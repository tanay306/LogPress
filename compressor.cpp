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

static std::regex g_num_regex(R"(\d+)");

// For in-memory line representation, now including the cluster ID
struct Entry {
    uint32_t file_id;
    uint32_t template_id;
    int cluster_id;  // New member to store the cluster ID
    std::vector<std::string> vars;
};

// parse result: template text plus numeric vars
struct ParseResult {
    std::string tpl;
    std::vector<std::string> vars;
    int cluster_id;  // New member to store the cluster ID
};

// detect numeric tokens and replace with <VAR>
static ParseResult make_template(const std::string& line, int cluster_id) {
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
    r.cluster_id = cluster_id;  // Assign the cluster ID
    return r;
}

// Helper to compress a buffer with zlib
static bool zlib_compress_buffer(const std::vector<char>& in_data,
                                 std::vector<char>& out_data) {
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
                                  const std::string& archive_path) {
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t total_input_size = 0;
    uint64_t bytes_read_so_far = 0;

    // Calculate the total size of all input files for progress tracking
    for (auto &f : input_files) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(f, ec);
        if (!ec) {
            total_input_size += sz;
        }
    }

    std::vector<std::string> templates;
    std::unordered_map<std::string, uint32_t> template_map;
    template_map.reserve(10000);

    std::vector<Entry> entries;
    entries.reserve(50000);

    std::vector<std::string> filenames;
    filenames.reserve(input_files.size());

    uint64_t uncompressed_size = 0;
    uint64_t total_lines = 0;

    // function for printing progress
    auto print_progress = [&](double ratio) {
        int pct = (int)(ratio * 100.0);
        std::cerr << "\rCompressing... " << pct << "%";
        std::cerr.flush();
    };

    // Read the clustered logs (logline + cluster_id) from the file
    for (uint32_t f_id = 0; f_id < (uint32_t)input_files.size(); f_id++) {
        filenames.push_back(input_files[f_id]);
        std::ifstream in_file(input_files[f_id]);
        if (!in_file.is_open()) {
            std::cerr << "\nCannot open " << input_files[f_id] << "\n";
            return false;
        }

        std::string line;
        while (std::getline(in_file, line)) {
            std::string logline;
            int cluster_id = -1;
            std::size_t delimiter_pos = line.find(" | Cluster ID: ");
            if (delimiter_pos != std::string::npos) {
                logline = line.substr(0, delimiter_pos);
                cluster_id = std::stoi(line.substr(delimiter_pos + 15)); // Skip " | Cluster ID: "
            }

            ++total_lines;
            uncompressed_size += (logline.size() + 1); // +1 for newline

            // parse the logline and assign the cluster_id
            auto parse = make_template(logline, cluster_id);

            // Dictionary look up
            auto it = template_map.find(parse.tpl);
            uint32_t tpl_id;
            if (it == template_map.end()) {
                tpl_id = (uint32_t)templates.size();
                templates.push_back(parse.tpl);
                template_map[parse.tpl] = tpl_id;
            } else {
                tpl_id = it->second;
            }

            // Store the entry including the cluster ID
            Entry e;
            e.file_id = f_id;
            e.template_id = tpl_id;
            e.cluster_id = parse.cluster_id; // Store cluster_id
            e.vars = std::move(parse.vars);
            entries.push_back(std::move(e));

            // Update bytes read so far and print progress
            bytes_read_so_far += line.size();
            double ratio = static_cast<double>(bytes_read_so_far) / total_input_size;
            print_progress(ratio);
        }
        in_file.close();
    }

    std::vector<char> uncompressed_data;
    uncompressed_data.reserve(entries.size() * 50 + 100);

    auto write_u32 = [&](uint32_t val) {
        char buf[4];
        std::memcpy(buf, &val, 4);
        uncompressed_data.insert(uncompressed_data.end(), buf, buf + 4);
    };

    const char T_magic[4] = {'T','M','P','L'};
    uncompressed_data.insert(uncompressed_data.end(), T_magic, T_magic + 4);

    uint32_t template_count = (uint32_t)templates.size();
    uint32_t line_count = (uint32_t)entries.size();
    write_u32(template_count);
    write_u32(line_count);

    for (auto &t : templates) {
        write_u32((uint32_t)t.size());
        uncompressed_data.insert(uncompressed_data.end(), t.begin(), t.end());
    }

    // Each entry now includes cluster_id
    for (auto &e : entries) {
        write_u32(e.file_id);
        write_u32(e.template_id);
        write_u32((uint32_t)e.vars.size());
        for (auto &v : e.vars) {
            write_u32((uint32_t)v.size());
            uncompressed_data.insert(uncompressed_data.end(), v.begin(), v.end());
        }
        write_u32(e.cluster_id); // Store the cluster_id
    }

    write_u32((uint32_t)filenames.size());
    for (auto &fn : filenames) {
        write_u32((uint32_t)fn.size());
        uncompressed_data.insert(uncompressed_data.end(), fn.begin(), fn.end());
    }

    std::vector<char> compressed_data;
    if (!zlib_compress_buffer(uncompressed_data, compressed_data)) {
        std::cerr << "zlib compression failed.\n";
        return false;
    }

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

    uint64_t final_size = std::filesystem::file_size(archive_path);
    double ratio = 0.0;
    if (final_size > 0) {
        ratio = double(uncompressed_size) / double(final_size);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    double compression_speed = (elapsed.count() > 0) ? (uncompressed_size / elapsed.count() / (1024 * 1024)) : 0;
    double percentage_reduction = (uncompressed_size > 0) ? ((1.0 - (double)cmp_size / uncompressed_size) * 100) : 0;

    std::cout << "\n--- Compression Metrics ---\n";
    std::cout << "Total lines read:          " << total_lines << "\n";
    std::cout << "Unique templates:          " << templates.size() << "\n";
    std::cout << "Uncompressed size (bytes): " << uncompressed_size << "\n";
    std::cout << "Compressed size (bytes):   " << final_size << "\n";
    std::cout << "Compression ratio:         " << ratio << "\n";
    std::cout << "Compression time:          " << elapsed.count() << " seconds\n";
    std::cout << "Compression speed:         " << compression_speed << " MB/s\n";
    std::cout << "Size reduction:            " << percentage_reduction << "%\n";

    return true;
}
