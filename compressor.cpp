#include "compressor.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <regex>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include <zlib.h>

// Structure for an in-memory log entry (now storing variable IDs)
struct Entry {
    uint32_t file_id;
    uint32_t template_id;
    std::vector<uint32_t> var_ids;
};

// Structure for parsing a line: templated text and raw variable strings.
struct ParseResult {
    std::string tpl;
    std::vector<std::string> vars;
};

static std::regex g_num_regex(R"((?:\d+[.:_-]?)+)");

// Replace numeric tokens with <VAR> and extract the raw tokens.
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

// Helper to write a uint32_t to a buffer.
static void write_u32(std::vector<char>& buf, uint32_t val) {
    char data[4];
    std::memcpy(data, &val, 4);
    buf.insert(buf.end(), data, data+4);
}

// New helper: compress a buffer using zlib with a custom dictionary.
static bool zlib_compress_buffer_with_dict(const std::vector<char>& in_data,
                                           std::vector<char>& out_data,
                                           const std::string& dict) {
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    int ret = deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                           15, 9, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        std::cerr << "deflateInit2 failed with code=" << ret << "\n";
        return false;
    }
    ret = deflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict.data()), dict.size());
    if (ret != Z_OK) {
        std::cerr << "deflateSetDictionary failed with code=" << ret << "\n";
        deflateEnd(&strm);
        return false;
    }
    
    uLongf bound = compressBound(in_data.size());
    out_data.resize(bound);
    
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in_data.data()));
    strm.avail_in = in_data.size();
    strm.next_out = reinterpret_cast<Bytef*>(out_data.data());
    strm.avail_out = out_data.size();
    
    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "deflate failed with code=" << ret << "\n";
        deflateEnd(&strm);
        return false;
    }
    out_data.resize(strm.total_out);
    deflateEnd(&strm);
    return true;
}

// Get a unique ID for a variable (deduplicates the raw variable strings).
static uint32_t get_variable_id(const std::string& var,
                                std::unordered_map<std::string, uint32_t>& var_map,
                                std::vector<std::string>& var_list) {
    auto it = var_map.find(var);
    if (it != var_map.end()) {
        return it->second;
    } else {
        uint32_t id = static_cast<uint32_t>(var_list.size());
        var_list.push_back(var);
        var_map[var] = id;
        return id;
    }
}

// Main compression function using block-based compression and dictionary-assisted compression.
bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path,
                                  size_t lines_per_block) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Determine total input size (for progress reporting)
    uint64_t total_input_size = 0;
    for (const auto& f : input_files) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(f, ec);
        if (!ec) {
            total_input_size += sz;
        }
    }
    
    // Containers for deduplication and storage.
    std::vector<std::string> templates;
    std::unordered_map<std::string, uint32_t> template_map;
    std::vector<std::string> filenames;
    std::vector<Entry> entries;
    std::unordered_map<std::string, uint32_t> variable_map;
    std::vector<std::string> variable_list;
    
    uint64_t total_lines = 0;
    uint64_t uncompressed_text_size = 0;
    uint64_t bytes_read_so_far = 0;
    
    auto print_progress = [&](double ratio, double speed) {
        int pct = static_cast<int>(ratio * 100.0);
        std::cerr << "\rCompressing... " << pct << "% | Speed: " << speed << " MB/s";
        std::cerr.flush();
    };
    double next_progress_threshold = 0.0;
    auto last_update_time = std::chrono::high_resolution_clock::now();
    
    // Process each input file.
    for (uint32_t f_id = 0; f_id < input_files.size(); f_id++) {
        filenames.push_back(input_files[f_id]);
        std::ifstream in_file(input_files[f_id]);
        if (!in_file.is_open()) {
            std::cerr << "\nCannot open " << input_files[f_id] << "\n";
            return false;
        }
        std::string line;
        while (std::getline(in_file, line)) {
            total_lines++;
            uncompressed_text_size += (line.size() + 1);
            
            ParseResult parse = make_template(line);
            uint32_t tpl_id;
            auto it_tpl = template_map.find(parse.tpl);
            if (it_tpl == template_map.end()) {
                tpl_id = static_cast<uint32_t>(templates.size());
                templates.push_back(parse.tpl);
                template_map[parse.tpl] = tpl_id;
            } else {
                tpl_id = it_tpl->second;
            }
            
            // Deduplicate variables.
            Entry e;
            e.file_id = f_id;
            e.template_id = tpl_id;
            for (const auto& var : parse.vars) {
                uint32_t var_id = get_variable_id(var, variable_map, variable_list);
                e.var_ids.push_back(var_id);
            }
            entries.push_back(std::move(e));
            
            // Update progress.
            std::streampos pos = in_file.tellg();
            if (pos > 0) {
                // Instead of adding pos, we assign it directly:
                bytes_read_so_far = static_cast<uint64_t>(pos);
                double ratio = static_cast<double>(bytes_read_so_far) / total_input_size;
                auto now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed = now - last_update_time;
                double speed = (elapsed.count() > 0) ? (bytes_read_so_far / elapsed.count() / (1024 * 1024)) : 0; // MB/s
                if (ratio >= next_progress_threshold) {
                    print_progress(ratio, speed);
                    next_progress_threshold += 0.01;
                }
            }
        }
        in_file.close();
    }
    std::cerr << "\n";
    
    // Increase block size if needed (bigger blocks usually yield better compression).
    size_t total_entries = entries.size();
    uint32_t block_count = static_cast<uint32_t>((total_entries + lines_per_block - 1) / lines_per_block);
    
    // Build the global header data.
    std::vector<char> global_data;
    // Write global header magic "TMPL".
    global_data.insert(global_data.end(), {'T','M','P','L'});
    // Write counts: templates, filenames, variables, and block count.
    write_u32(global_data, static_cast<uint32_t>(templates.size()));
    write_u32(global_data, static_cast<uint32_t>(filenames.size()));
    write_u32(global_data, static_cast<uint32_t>(variable_list.size()));
    write_u32(global_data, block_count);
    
    // Write templates section.
    for (const auto& tpl : templates) {
        write_u32(global_data, static_cast<uint32_t>(tpl.size()));
        global_data.insert(global_data.end(), tpl.begin(), tpl.end());
    }
    // Write filenames section.
    for (const auto& fn : filenames) {
        write_u32(global_data, static_cast<uint32_t>(fn.size()));
        global_data.insert(global_data.end(), fn.begin(), fn.end());
    }
    // Write variable dictionary section.
    for (const auto& var : variable_list) {
        write_u32(global_data, static_cast<uint32_t>(var.size()));
        global_data.insert(global_data.end(), var.begin(), var.end());
    }
    
    // Build a custom compression dictionary from the text fields.
    std::string compression_dict;
    for (const auto& tpl : templates) { compression_dict += tpl; }
    for (const auto& fn : filenames) { compression_dict += fn; }
    for (const auto& var : variable_list) { compression_dict += var; }
    
    // Process and compress each block.
    std::vector<char> blocks_data;
    size_t total_uncompressed_lines_size = 0;
    size_t total_compressed_lines_size = 0;
    size_t entry_index = 0;
    
    for (uint32_t b = 0; b < block_count; b++) {
        std::vector<char> block_uncompressed;
        uint32_t lines_in_block = 0;
        while (entry_index < total_entries && lines_in_block < lines_per_block) {
            const Entry& e = entries[entry_index++];
            lines_in_block++;
            write_u32(block_uncompressed, e.file_id);
            write_u32(block_uncompressed, e.template_id);
            write_u32(block_uncompressed, static_cast<uint32_t>(e.var_ids.size()));
            for (uint32_t var_id : e.var_ids) {
                write_u32(block_uncompressed, var_id);
            }
        }
        uint32_t block_uncompressed_size = static_cast<uint32_t>(block_uncompressed.size());
        total_uncompressed_lines_size += block_uncompressed_size;
        
        // Compress the block with our dictionary.
        std::vector<char> block_compressed;
        if (!zlib_compress_buffer_with_dict(block_uncompressed, block_compressed, compression_dict)) {
            std::cerr << "Block compression with dictionary failed.\n";
            return false;
        }
        uint32_t block_compressed_size = static_cast<uint32_t>(block_compressed.size());
        total_compressed_lines_size += block_compressed_size;
        
        // Write block header: number of lines, uncompressed size, compressed size.
        write_u32(blocks_data, lines_in_block);
        write_u32(blocks_data, block_uncompressed_size);
        write_u32(blocks_data, block_compressed_size);
        // Append the compressed block data.
        blocks_data.insert(blocks_data.end(), block_compressed.begin(), block_compressed.end());
    }
    
    // Write the final archive file.
    std::ofstream out(archive_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Cannot create archive file: " << archive_path << "\n";
        return false;
    }
    
    // Write file magic "TMZL".
    out.write("TMZL", 4);
    // Write the global header.
    out.write(global_data.data(), global_data.size());
    // Write the blocks.
    out.write(blocks_data.data(), blocks_data.size());
    out.close();
    
    uint64_t final_size = std::filesystem::file_size(archive_path);
    size_t total_global_uncompressed = 4 + global_data.size(); // file magic + header
    size_t total_uncompressed = total_global_uncompressed + total_uncompressed_lines_size;
    size_t total_compressed = total_global_uncompressed + total_compressed_lines_size;
    double ratio = (final_size > 0) ? double(total_uncompressed) / double(final_size) : 0;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double compression_speed = (elapsed.count() > 0) ? (total_uncompressed / elapsed.count() / (1024 * 1024)) : 0;
    double percentage_reduction = (total_uncompressed > 0) ? ((1.0 - double(total_compressed) / total_uncompressed) * 100) : 0;
    
    std::cout << "\n--- Compression Metrics (Template + Dictionary-assisted Block Compression) ---\n";
    std::cout << "Total lines read:          " << total_lines << "\n";
    std::cout << "Unique templates:          " << templates.size() << "\n";
    std::cout << "Unique variables:          " << variable_list.size() << "\n";
    std::cout << "Uncompressed text size:    " << uncompressed_text_size << " bytes\n";
    std::cout << "Global header size:        " << global_data.size() << " bytes\n";
    std::cout << "Uncompressed lines size:   " << total_uncompressed_lines_size << " bytes\n";
    std::cout << "Total uncompressed size:   " << total_uncompressed << " bytes\n";
    std::cout << "Total compressed size:     " << final_size << " bytes\n";
    std::cout << "Compression ratio:         " << ratio << "\n";
    std::cout << "Compression time:          " << elapsed.count() << " seconds\n";
    std::cout << "Compression speed:         " << compression_speed << " MB/s\n";
    std::cout << "Size reduction:            " << percentage_reduction << "%\n";
    std::cout << "--------------------------------------------------------------------------\n";
    
    return true;
}
