#include "compressor.hpp"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <regex>
#include <cstdint>
#include <vector>
#include <cstring>
#include <zlib.h> // link with -lz

/*
 * This code is the template-based approach (where we replace numeric tokens with <VAR>)
 * combined with a final zlib compression. Additionally, we now show progress while reading
 * large log files. We do this by summing the file sizes, counting bytes read so far, and
 * printing a progress percentage.
 *
 * Final file format on disk: "TMZL" + [4 bytes uncompressed_size] + [4 bytes compressed_size] + compressed_data
 * The uncompressed data inside is "TMPL" + dictionary, lines, filenames, exactly as before.
 */

static std::regex g_num_regex(R"(\d+)");

// For in-memory line representation
struct Entry {
    uint32_t file_id;
    uint32_t template_id;
    std::vector<std::string> vars;
};

// parse result: template text plus numeric vars
struct ParseResult {
    std::string tpl;
    std::vector<std::string> vars;
};

// detect numeric tokens and replace with <VAR>
static ParseResult make_template(const std::string& line) {
    ParseResult r;
    r.tpl.reserve(line.size());
    std::size_t startPos = 0;
    for (std::sregex_iterator it(line.begin(), line.end(), g_num_regex), end; it != end; ++it) {
        auto m = *it;
        auto matchStart = static_cast<std::size_t>(m.position());
        auto matchLen   = m.length();
        if (matchStart > startPos) {
            r.tpl.append(line, startPos, matchStart - startPos);
        }
        r.tpl += "<VAR>";
        r.vars.push_back(m.str());
        startPos = matchStart + matchLen;
    }
    if (startPos < line.size()) {
        r.tpl.append(line, startPos, line.size() - startPos);
    }
    return r;
}

// Helper to compress a buffer with zlib
static bool zlib_compress_buffer(const std::vector<char>& in_data,
                                 std::vector<char>& out_data)
{
    uLongf bound = compressBound((uLong)in_data.size());
    out_data.resize(bound);

    int ret = ::compress2(
        reinterpret_cast<Bytef*>(out_data.data()),
        &bound, // updated to actual compressed size
        reinterpret_cast<const Bytef*>(in_data.data()),
        (uLong)in_data.size(),
        Z_BEST_COMPRESSION
    );
    if (ret != Z_OK) {
        std::cerr << "zlib compress2 failed, code=" << ret << "\n";
        return false;
    }
    out_data.resize(bound);
    return true;
}

bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    // First, let's find total size of all input files (for progress).
    uint64_t total_input_size = 0;
    for (auto &f : input_files) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(f, ec);
        if (!ec) {
            total_input_size += sz;
        }
    }

    // We'll accumulate "raw template-based archive" in memory, then zlib-compress it
    std::vector<std::string> templates;
    std::unordered_map<std::string, uint32_t> template_map;
    template_map.reserve(10000);

    std::vector<Entry> entries;
    entries.reserve(50000);

    std::vector<std::string> filenames;
    filenames.reserve(input_files.size());

    uint64_t uncompressed_size = 0; // total textual bytes read
    uint64_t total_lines       = 0; // number of log lines
    uint64_t bytes_read_so_far = 0; // for progress

    // function for printing progress
    auto print_progress = [&](double ratio, double speed) {
        // ratio = fraction of total_input_size read
        // example: ratio=0.37 => 37%
        int pct = (int)(ratio * 100.0);
        std::cerr << "\rCompressing... " << pct << "% | Speed: " << speed << " MB/s";
        std::cerr.flush();
    };
    double next_progress_threshold = 0.0;
    auto last_update_time = std::chrono::high_resolution_clock::now();


    for (uint32_t f_id = 0; f_id < (uint32_t)input_files.size(); f_id++) {
        filenames.push_back(input_files[f_id]);

        std::ifstream in_file(input_files[f_id]);
        if (!in_file.is_open()) {
            std::cerr << "\nCannot open " << input_files[f_id] << "\n";
            return false;
        }

        std::string line;
        while (true) {
            // We read line by line
            std::streampos before_read_pos = in_file.tellg();
            if (!std::getline(in_file, line)) {
                if (in_file.eof()) {
                    break;
                }
                std::cerr << "\nError reading from " << input_files[f_id] << "\n";
                return false;
            }
            std::streampos after_read_pos = in_file.tellg();
            if (after_read_pos > before_read_pos) {
                // bytes read from file
                bytes_read_so_far += (uint64_t)(after_read_pos - before_read_pos);
            }

            ++total_lines;
            // track uncompressed text size
            uncompressed_size += (line.size() + 1); // +1 for newline

            // parse line => template, vars
            auto parse = make_template(line);

            // dictionary look up
            auto it = template_map.find(parse.tpl);
            uint32_t tpl_id;
            if (it == template_map.end()) {
                tpl_id = (uint32_t)templates.size();
                templates.push_back(parse.tpl);
                template_map[parse.tpl] = tpl_id;
            } else {
                tpl_id = it->second;
            }

            // store
            Entry e;
            e.file_id     = f_id;
            e.template_id = tpl_id;
            e.vars        = std::move(parse.vars);
            entries.push_back(std::move(e));

            // progress check
            if (total_input_size > 0) {
                double ratio = static_cast<double>(bytes_read_so_far) / total_input_size;

                auto now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed = now - last_update_time;
                double speed = (elapsed.count() > 0) ? (bytes_read_so_far / elapsed.count() / (1024 * 1024)) : 0; // MB/s

                // If ratio >= next_progress_threshold, print and increment threshold
                if (ratio >= next_progress_threshold) {
                    print_progress(ratio, speed);
                    next_progress_threshold += 0.01; // e.g. print every 1% of total
                }
            }
        }
        in_file.close();
    }

    // final progress at 100% if total_input_size was known
    if (total_input_size > 0) {
        print_progress(1.0, 0);
    }
    std::cerr << "\n"; // newline after progress

    // now build the in-memory "TMPL" structure
    std::vector<char> uncompressed_data;
    uncompressed_data.reserve(entries.size() * 50 + 100);

    auto write_u32 = [&](uint32_t val) {
        char buf[4];
        std::memcpy(buf, &val, 4);
        uncompressed_data.insert(uncompressed_data.end(), buf, buf+4);
    };

    // "TMPL" magic
    const char T_magic[4] = {'T','M','P','L'};
    uncompressed_data.insert(uncompressed_data.end(), T_magic, T_magic+4);

    // template_count, line_count
    uint32_t template_count = (uint32_t)templates.size();
    uint32_t line_count     = (uint32_t)entries.size();
    write_u32(template_count);
    write_u32(line_count);

    // each template
    for (auto &t : templates) {
        write_u32((uint32_t)t.size());
        uncompressed_data.insert(uncompressed_data.end(), t.begin(), t.end());
    }

    // each line => file_id, template_id, var_count, then var strings
    for (auto &e : entries) {
        write_u32(e.file_id);
        write_u32(e.template_id);
        write_u32((uint32_t)e.vars.size());
        for (auto &v : e.vars) {
            write_u32((uint32_t)v.size());
            uncompressed_data.insert(uncompressed_data.end(), v.begin(), v.end());
        }
    }

    // filenames
    write_u32((uint32_t)filenames.size());
    for (auto &fn : filenames) {
        write_u32((uint32_t)fn.size());
        uncompressed_data.insert(uncompressed_data.end(), fn.begin(), fn.end());
    }

    // compress with zlib
    std::vector<char> compressed_data;
    if (!zlib_compress_buffer(uncompressed_data, compressed_data)) {
        std::cerr << "zlib compression failed.\n";
        return false;
    }

    // final file => "TMZL" + [uncompressed size, compressed size] + compressed_data
    std::ofstream out(archive_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Cannot create " << archive_path << "\n";
        return false;
    }

    const char Z_magic[4] = {'T','M','Z','L'};
    out.write(Z_magic, 4);

    uint32_t unc_size = (uint32_t)uncompressed_data.size();
    uint32_t cmp_size = (uint32_t)compressed_data.size();
    out.write(reinterpret_cast<const char*>(&unc_size), 4);
    out.write(reinterpret_cast<const char*>(&cmp_size), 4);
    out.write(compressed_data.data(), compressed_data.size());
    out.close();

    // gather final size from filesystem
    uint64_t final_size = std::filesystem::file_size(archive_path);
    double ratio = 0.0;
    if (final_size > 0) {
        ratio = double(uncompressed_size) / double(final_size);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    double compression_speed = (elapsed.count() > 0) ? (uncompressed_size / elapsed.count() / (1024 * 1024)) : 0; // MB/s
    double percentage_reduction = (uncompressed_size > 0) ? ((1.0 - (double)cmp_size / uncompressed_size) * 100) : 0;

    std::cout << "\n--- Compression Metrics (Template + zlib) ---\n";
    std::cout << "Total lines read:          " << total_lines << "\n";
    std::cout << "Unique templates:          " << templates.size() << "\n";
    std::cout << "Uncompressed size (bytes): " << uncompressed_size << "\n";
    std::cout << "Compressed size (bytes):   " << final_size << "\n";
    std::cout << "Compression ratio:         " << ratio << " (uncompressed/.myclp)\n";
    std::cout << "Compression time:          " << elapsed.count() << " seconds\n";
    std::cout << "Compression speed:         " << compression_speed << " MB/s\n";
    std::cout << "Size reduction:            " << percentage_reduction << "%\n";
    std::cout << "----------------------------------------------\n";

    return true;
}
