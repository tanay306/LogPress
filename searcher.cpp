#include "searcher.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <regex>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <zlib.h>

// New helper: decompress a buffer using zlib with a custom dictionary.
static bool zlib_decompress_buffer_with_dict(const std::vector<char>& in_data,
                                             size_t uncompressed_size,
                                             std::vector<char>& out_data,
                                             const std::string& dict) {
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    int ret = inflateInit2(&strm, 15);
    if (ret != Z_OK) {
        std::cerr << "inflateInit2 failed with code=" << ret << "\n";
        return false;
    }
    
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in_data.data()));
    strm.avail_in = in_data.size();
    
    out_data.resize(uncompressed_size);
    strm.next_out = reinterpret_cast<Bytef*>(out_data.data());
    strm.avail_out = uncompressed_size;
    
    ret = inflate(&strm, Z_FINISH);
    if (ret == Z_NEED_DICT) {
        ret = inflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict.data()), dict.size());
        if (ret != Z_OK) {
            std::cerr << "inflateSetDictionary failed with code=" << ret << "\n";
            inflateEnd(&strm);
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    }
    if (ret != Z_STREAM_END) {
        std::cerr << "inflate failed with code=" << ret << "\n";
        inflateEnd(&strm);
        return false;
    }
    if (strm.total_out != uncompressed_size) {
        std::cerr << "Uncompressed size mismatch in search with dictionary.\n";
        inflateEnd(&strm);
        return false;
    }
    inflateEnd(&strm);
    return true;
}

// Helper: read a uint32_t from a buffer.
static uint32_t read_u32(const char*& p) {
    uint32_t val;
    std::memcpy(&val, p, 4);
    p += 4;
    return val;
}

// Convert a wildcard pattern to a regex string.
static std::string wildcard_to_regex(const std::string& pattern) {
    std::string regex_pattern;
    for (char ch : pattern) {
        switch(ch) {
            case '*': regex_pattern += ".*"; break;
            case '?': regex_pattern += "."; break;
            case '.': regex_pattern += "\\."; break;
            case '^': regex_pattern += "\\^"; break;
            case '$': regex_pattern += "\\$"; break;
            default: regex_pattern.push_back(ch);
        }
    }
    return regex_pattern;
}

bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term) {
    auto search_start = std::chrono::high_resolution_clock::now();
    
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Cannot open archive: " << archive_path << "\n";
        return false;
    }
    
    // Read file magic "TMZL".
    char file_magic[4];
    in.read(file_magic, 4);
    if (in.gcount() < 4 || std::strncmp(file_magic, "TMZL", 4) != 0) {
        std::cerr << "Not a valid TMZL archive.\n";
        return false;
    }
    
    // Read global header magic "TMPL".
    char global_magic[4];
    in.read(global_magic, 4);
    if (in.gcount() < 4 || std::strncmp(global_magic, "TMPL", 4) != 0) {
        std::cerr << "Missing TMPL global header.\n";
        return false;
    }
    
    uint32_t template_count = 0;
    uint32_t filename_count = 0;
    uint32_t variable_count = 0;
    uint32_t block_count = 0;
    
    in.read(reinterpret_cast<char*>(&template_count), 4);
    in.read(reinterpret_cast<char*>(&filename_count), 4);
    in.read(reinterpret_cast<char*>(&variable_count), 4);
    in.read(reinterpret_cast<char*>(&block_count), 4);
    
    // Read templates.
    std::vector<std::string> templates(template_count);
    for (uint32_t i = 0; i < template_count; i++) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::string tpl(len, '\0');
        in.read(&tpl[0], len);
        templates[i] = tpl;
    }
    
    // Read filenames.
    std::vector<std::string> filenames(filename_count);
    for (uint32_t i = 0; i < filename_count; i++) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::string fn(len, '\0');
        in.read(&fn[0], len);
        filenames[i] = fn;
    }
    
    // Read variable dictionary.
    std::vector<std::string> variable_dict(variable_count);
    for (uint32_t i = 0; i < variable_count; i++) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::string var(len, '\0');
        in.read(&var[0], len);
        variable_dict[i] = var;
    }
    
    // Build the same dictionary used during compression.
    std::string compression_dict;
    for (const auto& tpl : templates) { compression_dict += tpl; }
    for (const auto& fn : filenames) { compression_dict += fn; }
    for (const auto& var : variable_dict) { compression_dict += var; }
    
    // Prepare regex matching if wildcards are present.
    std::regex search_regex;
    bool use_regex = false;
    if (search_term.find('*') != std::string::npos || search_term.find('?') != std::string::npos) {
        std::string regex_str = wildcard_to_regex(search_term);
        try {
            search_regex = std::regex(regex_str);
            use_regex = true;
        } catch (std::regex_error& e) {
            std::cerr << "Invalid wildcard pattern.\n";
            return false;
        }
    }
    
    bool found_any = false;
    
    // Process each block.
    for (uint32_t b = 0; b < block_count; b++) {
        uint32_t lines_in_block = 0;
        uint32_t block_uncompressed_size = 0;
        uint32_t block_compressed_size = 0;
        in.read(reinterpret_cast<char*>(&lines_in_block), 4);
        in.read(reinterpret_cast<char*>(&block_uncompressed_size), 4);
        in.read(reinterpret_cast<char*>(&block_compressed_size), 4);
        
        std::vector<char> block_compressed(block_compressed_size);
        in.read(block_compressed.data(), block_compressed_size);
        if (in.gcount() < block_compressed_size) {
            std::cerr << "Incomplete block data.\n";
            return false;
        }
        
        std::vector<char> block_uncompressed;
        if (!zlib_decompress_buffer_with_dict(block_compressed, block_uncompressed_size, block_uncompressed, compression_dict)) {
            std::cerr << "Block decompression with dictionary failed in search.\n";
            return false;
        }
        
        const char* p = block_uncompressed.data();
        for (uint32_t i = 0; i < lines_in_block; i++) {
            uint32_t file_id = read_u32(p);
            uint32_t tpl_id = read_u32(p);
            uint32_t var_count_line = read_u32(p);
            std::vector<uint32_t> var_ids(var_count_line);
            for (uint32_t j = 0; j < var_count_line; j++) {
                var_ids[j] = read_u32(p);
            }
            
            if (tpl_id >= templates.size()) {
                std::cerr << "Invalid template ID in search.\n";
                return false;
            }
            const std::string& tpl = templates[tpl_id];
            std::string reconstructed;
            reconstructed.reserve(tpl.size() + 20 * var_ids.size());
            size_t start = 0;
            size_t var_index = 0;
            while (true) {
                size_t pos = tpl.find("<VAR>", start);
                if (pos == std::string::npos) {
                    reconstructed.append(tpl, start, tpl.size()-start);
                    break;
                }
                reconstructed.append(tpl, start, pos - start);
                if (var_index < var_ids.size() && var_ids[var_index] < variable_dict.size()) {
                    reconstructed += variable_dict[var_ids[var_index]];
                    var_index++;
                } else {
                    reconstructed += "???";
                }
                start = pos + 5;
            }
            
            bool match = false;
            if (search_term.empty()) {
                match = true;
            } else if (use_regex) {
                match = std::regex_search(reconstructed, search_regex);
            } else {
                match = (reconstructed.find(search_term) != std::string::npos);
            }
            
            if (match) {
                found_any = true;
                std::string fn = (file_id < filenames.size() ? filenames[file_id] : "UNKNOWN");
                std::cout << fn << ": " << reconstructed << "\n";
            }
        }
    }
    
    auto search_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> search_duration = search_end - search_start;
    std::cout << "Search time: " << search_duration.count() << " seconds\n";
    
    if (!found_any) {
        std::cout << "No matches found.\n";
    }
    
    return true;
}
