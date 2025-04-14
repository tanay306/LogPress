#ifndef _Alignof
#define _Alignof(x) __alignof__(x)
#endif

#include "compressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <iomanip>

int main() {
    // Hardcoded test lines
    std::vector<std::string> test_lines = {
        "10.251.34.15 081111 blk_-1234 size 34864",
        "192.168.0.1 123456 error=101",
        "2023-04-12 14:23:05 88999"
    };

    std::string archive_path = "test.tcdb";
    std::vector<std::string> templates, variables, files = {"test.log"};
    std::vector<VarType> types;
    std::vector<char> block_data;

    std::unordered_map<std::string, uint32_t> tpl_map, var_map;
    for (const std::string& line : test_lines) {
        ParseResult pr = make_typed_template(line);
        uint32_t tpl_id = tpl_map.emplace(pr.tpl, templates.size()).first->second;
        if (tpl_id == templates.size()) templates.push_back(pr.tpl);

        uint32_t file_id = 0;
        write_u32(block_data, file_id);  // New field
        write_u32(block_data, tpl_id);
        write_u32(block_data, pr.vars.size());

        for (size_t i = 0; i < pr.vars.size(); ++i) {
            const std::string& v = pr.vars[i];
            uint32_t var_id = var_map.emplace(v, variables.size()).first->second;
            if (var_id == variables.size()) {
                variables.push_back(v);
                types.push_back(pr.types[i]);
            }
            write_u32(block_data, var_id);
        }
    }

    // Create .meta.db
    sqlite3* db = nullptr;
    if (!initialize_db(db, archive_path + ".meta.db")) {
        std::cerr << "Failed to init SQLite.\n";
        return 1;
    }
    store_templates_and_variables(db, templates, variables, types, files);
    sqlite3_close(db);

    // Build dictionary and hash
    std::string dict;
    for (const auto& s : templates) dict += s;
    for (const auto& v : variables) dict += v;
    for (const auto& f : files) dict += f;
    
    std::cout << "Dictionary size: " << dict.size() << "\n";
    std::ofstream dict_out("compression.dict");
    dict_out << dict;
    dict_out.close();


    // Compress block
    std::vector<char> compressed;
    if (!zlib_compress_block(block_data, compressed, dict)) {
        std::cerr << "Compression failed.\n";
        return 1;
    }

    std::ofstream out(archive_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open archive.\n";
        return 1;
    }

    out.write("TCDZ", 4);
    uint32_t lines = test_lines.size();
    uint32_t blk_size = static_cast<uint32_t>(block_data.size());
    uint32_t comp_size = static_cast<uint32_t>(compressed.size());
    out.write(reinterpret_cast<const char*>(&lines), 4);
    out.write(reinterpret_cast<const char*>(&blk_size), 4);
    out.write(reinterpret_cast<const char*>(&comp_size), 4);
    out.write(compressed.data(), compressed.size());

    std::cout << "✅ test.tcdb written with 1 block / " << lines << " lines.\n";
    return 0;
}
