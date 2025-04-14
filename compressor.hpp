#pragma once

#include <string>
#include <vector>

// Log variable classification
enum class VarType {
    IP,     // IP address: 192.168.0.1
    TS,     // Timestamp: 081111 or 2023-04-12
    NUM     // Generic number: 34864, 14-999
};

// Template + variable extraction result
struct ParseResult {
    std::string tpl;
    std::vector<std::string> vars;
    std::vector<VarType> types;
};

// Main compression entry point
bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path,
                                  size_t lines_per_block = 4096);

// Extraction + classification for test harness use
ParseResult make_typed_template(const std::string& line);

// Write a uint32_t into a binary buffer
void write_u32(std::vector<char>& buf, uint32_t val);

// Zlib compression for testing
bool zlib_compress_block(const std::vector<char>& in,
                         std::vector<char>& out,
                         const std::string& dict);
