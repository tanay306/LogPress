#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <filesystem>
#include <cstring>
#include <zlib.h>

#include "sqlite_helper.hpp"
#include "searcher.hpp"

uint32_t read_u32(std::ifstream &in) {
    uint32_t v;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

uint32_t read_u32_mem(const char *&p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    p += 4;
    return v;
}

bool zlib_decompress_block(const std::vector<char> &in_data,
                           size_t uncompressed_size,
                           std::vector<char> &out_data,
                           const std::string &dict) {
    z_stream strm{};
    if (inflateInit2(&strm, 15) != Z_OK) {
        return false;
    }
    out_data.resize(uncompressed_size);
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in_data.data()));
    strm.avail_in = static_cast<uInt>(in_data.size());
    strm.next_out = reinterpret_cast<Bytef *>(out_data.data());
    strm.avail_out = static_cast<uInt>(out_data.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_NEED_DICT && !dict.empty()) {
        if (inflateSetDictionary(&strm, reinterpret_cast<const Bytef *>(dict.data()), dict.size()) != Z_OK) {
            inflateEnd(&strm);
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    }
    bool ok = (ret == Z_STREAM_END && strm.total_out == uncompressed_size);
    inflateEnd(&strm);
    return ok;
}

VarType parse_filter_type(const std::string &input) {
    if (input == "IP")  return VarType::IP;
    if (input == "TS")  return VarType::TS;
    if (input == "NUM") return VarType::NUM;
    return VarType::NUM;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n  " << argv[0] << " <archive.tcdb> [search_term] [--type=IP|TS|NUM]\n";
        return 1;
    }

    std::string archive_path = argv[1];
    std::string search_term;
    std::string type_filter;
    if (argc >= 3) {
        search_term = argv[2];
    }
    if (argc >= 4 && std::string(argv[3]).rfind("--type=", 0) == 0) {
        type_filter = std::string(argv[3]).substr(7);
    }

    // 1) open the archive
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "❌ Cannot open archive: " << archive_path << "\n";
        return 1;
    }

    // 2) read magic
    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "TCDZ", 4) != 0) {
        std::cerr << "❌ Invalid archive format.\n";
        return 1;
    }

    // 3) load metadata
    sqlite3* db = nullptr;
    std::vector<std::string> templates, variables, filenames;
    std::vector<VarType> types;

    std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    std::cout << "📂 Opening meta.db at: " << meta_path << "\n";
    if (sqlite3_open(meta_path.string().c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open meta.db.\n";
        return 1;
    }
    if (!load_templates_and_variables(db, templates, variables, types, filenames)) {
        std::cerr << "❌ Failed to load from meta.db\n";
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);

    // build dictionary
    std::string dict;
    dict.reserve(templates.size()*40 + variables.size()*20 + filenames.size()*10);
    for (auto &t : templates)  dict += t;
    for (auto &v : variables)  dict += v;
    for (auto &f : filenames)  dict += f;

    std::cout << "[DEBUG] templates.size()=" << templates.size()
              << ", variables.size()=" << variables.size()
              << ", types.size()=" << types.size()
              << ", filenames.size()=" << filenames.size() << "\n";

    // 4) parse search logic
    bool use_regex = false;
    std::regex search_regex;
    std::string rough_substr;
    if (!search_term.empty() && 
        (search_term.find('*') != std::string::npos || search_term.find('?') != std::string::npos)) 
    {
        std::string pattern;
        for (char c : search_term) {
            if      (c == '*')  pattern += ".*";
            else if (c == '?')  pattern += ".";
            else if (std::string(".^$\\[](){}+|").find(c) != std::string::npos) {
                pattern += '\\'; pattern += c;
            } else {
                pattern += c;
            }
        }
        search_regex = std::regex(pattern);
        use_regex = true;
        size_t starPos = search_term.find('*');
        if (starPos != std::string::npos && starPos > 0) {
            rough_substr = search_term.substr(0, starPos);
        }
    }

    VarType filter_type = parse_filter_type(type_filter);
    bool apply_type_filter = !type_filter.empty();
    size_t match_count = 0;
    int block_index = 0;

    // 5) read blocks
    while (in.peek() != EOF) {
        if (!in.good()) break;
        uint32_t lines  = read_u32(in);
        if (!in.good()) break;
        uint32_t uncomp = read_u32(in);
        if (!in.good()) break;
        uint32_t comp   = read_u32(in);
        if (!in.good()) break;

        std::cout << "[DEBUG] block#" << block_index 
                  << " lines=" << lines 
                  << " uncomp=" << uncomp 
                  << " comp=" << comp << "\n";
        block_index++;

        std::vector<char> comp_buf(comp);
        in.read(comp_buf.data(), comp);
        if ((size_t)in.gcount() < comp) {
            std::cerr << "❌ Truncated block.\n";
            return 1;
        }

        std::vector<char> block;
        if (!zlib_decompress_block(comp_buf, uncomp, block, dict)) {
            std::cerr << "❌ Decompression failed at block#" << (block_index - 1) << "\n";
            return 1;
        }

        const char* p = block.data();
        size_t block_size = block.size();
        for (uint32_t iLine = 0; iLine < lines; iLine++) {
            // we expect at least 3 * 4 bytes => file_id, tpl_id, var_count
            if ((p + 12) > (block.data() + block_size)) {
                std::cerr << "❌ Block data truncated reading line#" << iLine << "\n";
                return 1;
            }

            // *** READ file_id FIRST ***
            uint32_t file_id   = read_u32_mem(p); // SHIFT
            uint32_t tpl_id    = read_u32_mem(p); // SHIFT
            uint32_t var_count = read_u32_mem(p);

            if ((p + var_count*4) > (block.data() + block_size)) {
                std::cerr << "❌ Block data truncated reading var_ids at line#" 
                          << iLine << " (var_count=" << var_count << ")\n";
                return 1;
            }
            std::vector<uint32_t> var_ids(var_count);
            for (uint32_t j = 0; j < var_count; j++) {
                var_ids[j] = read_u32_mem(p);
            }

            // bounds check on tpl_id
            if (tpl_id >= templates.size()) {
                std::cerr << "[DEBUG] tpl_id out of range => "
                          << tpl_id << " >= templates.size()=" << templates.size() << "\n";
                continue;
            }

            // type filter => see if line has that type
            bool hasType = false;
            if (apply_type_filter) {
                for (uint32_t v_id : var_ids) {
                    if (v_id < types.size() && types[v_id] == filter_type) {
                        hasType = true;
                        break;
                    }
                }
            }
            if (apply_type_filter && !hasType) {
                // skip line
                continue;
            }

            // reconstruct line
            const std::string &tpl = templates[tpl_id];
            std::string reconstructed;
            reconstructed.reserve(tpl.size() + var_count * 12);

            size_t last = 0, vi = 0;
            while (true) {
                size_t pos = tpl.find("<VAR>", last);
                if (pos == std::string::npos) {
                    reconstructed.append(tpl, last);
                    break;
                }
                reconstructed.append(tpl, last, pos - last);

                std::string val = "???";
                if (vi < var_ids.size()) {
                    uint32_t v_id = var_ids[vi];
                    if (v_id < variables.size()) {
                        val = variables[v_id];
                    }
                }
                reconstructed += val;
                last = pos + 5;
                vi++;
            }

            // now do search
            bool matched = false;
            if (search_term.empty()) {
                matched = true;
            }
            else if (use_regex) {
                if (!rough_substr.empty() && reconstructed.find(rough_substr) == std::string::npos) {
                    matched = false;
                } else {
                    matched = std::regex_search(reconstructed, search_regex);
                }
            } else {
                matched = (reconstructed.find(search_term) != std::string::npos);
            }

            if (matched) {
                match_count++;
                std::cout << reconstructed << "\n";
            }
        }
    }

    std::cout << "\nFound " << match_count << " matches.\n";
    return 0;
}