#include "compressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <unordered_map>
#include <zlib.h>
#include <cstring>

// Utility to write uint32_t
void write_u32(std::vector<char>& buf, uint32_t v) {
    char tmp[4];
    std::memcpy(tmp, &v, 4);
    buf.insert(buf.end(), tmp, tmp + 4);
}

// Classification logic
VarType classify_var(const std::string& v) {
    // IPv4: each octet must be 0-255
    static std::regex ip(
        R"(^((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$)"
    );
    static std::regex ts_pattern_1(
        R"(^(0[0-9]|1[0-9]|2[0-3])[:\-]?([0-5][0-9])[:\-]?([0-5][0-9])$)"
    );
    static std::regex ts_pattern_2(
        R"(^((19|20)\d\d)[-/](0[1-9]|1[0-2])[-/](0[1-9]|[12]\d|3[01])$)"
    );
    static std::regex ts_pattern_3(
        R"(^\d{8,14}$)"
    );

    if (std::regex_match(v, ip)) return VarType::IP;
    if (std::regex_match(v, ts_pattern_1) ||
        std::regex_match(v, ts_pattern_2) ||
        std::regex_match(v, ts_pattern_3)) return VarType::TS;

    return VarType::NUM;
}

static std::regex g_var_regex(R"([+\-]?\d+(?:[._:\-]\d+)*)");

ParseResult make_typed_template(const std::string& line) {
    ParseResult r;
    size_t last = 0;
    for (std::sregex_iterator it(line.begin(), line.end(), g_var_regex), end; it != end; ++it) {
        auto m = *it;
        r.tpl.append(line, last, m.position() - last);
        r.tpl += "<VAR>";
        r.vars.push_back(m.str());
        r.types.push_back(classify_var(m.str()));
        last = m.position() + m.length();
    }
    r.tpl.append(line, last);
    return r;
}

// Zlib compression
bool zlib_compress_block(const std::vector<char>& in, std::vector<char>& out, const std::string& dict) {
    z_stream strm{};
    if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    deflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict.data()), dict.size());
    out.resize(compressBound(in.size()));
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    strm.avail_in = static_cast<uInt>(in.size());
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) { deflateEnd(&strm); return false; }
    out.resize(strm.total_out);
    deflateEnd(&strm);
    return true;
}

bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path,
                                  size_t lines_per_block) {
    std::unordered_map<std::string, uint32_t> tpl_map, var_map, file_map;
    std::vector<std::string> templates, variables, files;
    std::vector<VarType> var_types;
    std::vector<char> current_block;
    std::vector<std::vector<char>> blocks;
    size_t total_lines = 0;

    for (const auto& file : input_files) {
        std::ifstream in(file);
        if (!in) { std::cerr << "File open failed: " << file << "\n"; return false; }

        uint32_t file_id = file_map.emplace(file, files.size()).first->second;
        if (file_id == files.size()) files.push_back(file);

        std::string line;
        while (std::getline(in, line)) {
            ParseResult pr = make_typed_template(line);

            if (pr.tpl.empty()) continue;

            uint32_t tpl_id = tpl_map.emplace(pr.tpl, templates.size()).first->second;
            if (tpl_id == templates.size()) templates.push_back(pr.tpl);

            write_u32(current_block, file_id);
            write_u32(current_block, tpl_id);
            write_u32(current_block, pr.vars.size());

            for (size_t i = 0; i < pr.vars.size(); ++i) {
                const std::string& v = pr.vars[i];
                uint32_t var_id = var_map.emplace(v, variables.size()).first->second;
                if (var_id == variables.size()) {
                    variables.push_back(v);
                    var_types.push_back(pr.types[i]);
                }
                write_u32(current_block, var_id);
            }

            total_lines++;
            if (total_lines % lines_per_block == 0) {
                blocks.push_back(std::move(current_block));
                current_block.clear();
            }
        }
    }

    if (!current_block.empty()){
        blocks.push_back(std::move(current_block));
    }


    std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    std::cout << "📂 Opening meta.db at: " << meta_path << "\n";

    // SQLite metadata
    sqlite3* db = nullptr;
    if (!initialize_db(db, archive_path + ".meta.db")) {
        std::cerr << "Failed to init SQLite.\n";
        return false;
    }
    store_templates_and_variables(db, templates, variables, var_types, files);
    sqlite3_close(db);

    // Build dictionary
    std::string dict;
    for (const auto& s : templates) dict += s;
    for (const auto& v : variables) dict += v;
    for (const auto& f : files) dict += f;

    std::ofstream debug_dict("compression.dict");
    debug_dict << dict;
    debug_dict.close();

    std::ofstream out(archive_path, std::ios::binary);
    if (!out) return false;
    out.write("TCDZ", 4);

    for (auto& blk : blocks) {
        std::vector<char> comp;
        if (!zlib_compress_block(blk, comp, dict)) {
            std::cerr << "Block compression failed.\n";
            return false;
        }

        size_t lines = 0, i = 0;
        while (i < blk.size()) {
            lines++;
            uint32_t var_offset = i + 8; // file_id (4) + tpl_id (4)
            uint32_t var_count;
            std::memcpy(&var_count, &blk[var_offset], 4);
            i = var_offset + 4 + var_count * 4;
        }

        uint32_t lines_val = static_cast<uint32_t>(lines);
        uint32_t blk_size = static_cast<uint32_t>(blk.size());
        uint32_t comp_size = static_cast<uint32_t>(comp.size());
        out.write(reinterpret_cast<const char*>(&lines_val), 4);
        out.write(reinterpret_cast<const char*>(&blk_size), 4);
        out.write(reinterpret_cast<const char*>(&comp_size), 4);
        out.write(comp.data(), comp.size());
    }
    std::cout << "[DEBUG] Final: variables.size()=" << variables.size()
          << ", var_types.size()=" << var_types.size() << "\n";

    std::cout << "📦 Compressed " << total_lines << " lines in " << blocks.size() << " blocks.\n";
    std::cout << "📊 Templates: " << templates.size()
              << ", Variables: " << variables.size()
              << ", Files: " << files.size() << "\n";
    std::cout << "🧠 Dict Size: " << dict.size() << " bytes (saved to compression.dict)\n";
    return true;
}
