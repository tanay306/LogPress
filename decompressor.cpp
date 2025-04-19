#include "decompressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <zlib.h>
#include <cstring>

static uint32_t read_u32(std::ifstream& in) {
    uint32_t v;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

static uint32_t read_u32_mem(const char*& p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    p += 4;
    return v;
}

static bool zlib_decompress_block(const std::vector<char>& in_data,
                                  size_t uncompressed_size,
                                  std::vector<char>& out_data,
                                  const std::string& dict) {
    z_stream strm{};
    if (inflateInit2(&strm, 15) != Z_OK) return false;

    out_data.resize(uncompressed_size);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in_data.data()));
    strm.avail_in = static_cast<uInt>(in_data.size());
    strm.next_out = reinterpret_cast<Bytef*>(out_data.data());
    strm.avail_out = static_cast<uInt>(out_data.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_NEED_DICT && !dict.empty()) {
        if (inflateSetDictionary(&strm, reinterpret_cast<const Bytef*>(dict.data()), dict.size()) != Z_OK) {
            inflateEnd(&strm);
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    }

    bool ok = (ret == Z_STREAM_END && strm.total_out == uncompressed_size);
    inflateEnd(&strm);
    return ok;
}

bool decompress_files_template_zlib(const std::string& archive_path,
                                    const std::string& output_folder) {
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "❌ Cannot open archive: " << archive_path << "\n";
        return false;
    }

    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "TCDZ", 4) != 0) {
        std::cerr << "❌ Invalid archive format.\n";
        return false;
    }

    // Load metadata
    sqlite3* db = nullptr;
    std::vector<std::string> templates, variables, filenames;
    std::vector<VarType> types;

    // std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    std::filesystem::path archive_p(archive_path);
    std::filesystem::path meta_path = "./db/" + (archive_p.filename().string() + ".meta.db");
    std::cout << "📂 Opening meta.db at: " << meta_path << "\n";

    
    if (sqlite3_open(meta_path.string().c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open existing meta.db: "
                  << archive_path + ".meta.db" << "\n";
        return false;
    }
    if (!load_templates_and_variables(db, templates, variables, types, filenames)) {
        std::cerr << "❌ Failed to load from meta.db\n";
        return false;
    }
    sqlite3_close(db);

    // Build dictionary
    std::string dict;
    for (const auto& s : templates) dict += s;
    for (const auto& v : variables) dict += v;
    for (const auto& f : filenames) dict += f;

    std::ofstream dict_out("decompression.dict");
    dict_out << dict;
    dict_out.close();

    std::filesystem::create_directories(output_folder);

    std::vector<std::ofstream> out_streams(filenames.size());
    for (size_t i = 0; i < filenames.size(); ++i) {
        std::filesystem::path path = output_folder;
        path /= std::filesystem::path(filenames[i]).filename();
        out_streams[i].open(path);
        if (!out_streams[i].is_open()) {
            std::cerr << "❌ Cannot open output file: " << path << "\n";
            return false;
        }
    }

    size_t ip_count = 0, ts_count = 0, num_count = 0;
    size_t total_lines = 0;
    int block_id = 0;

    while (in.peek() != EOF) {
        uint32_t lines = read_u32(in);
        uint32_t uncompressed_size = read_u32(in);
        uint32_t compressed_size = read_u32(in);

        std::vector<char> compressed(compressed_size);
        in.read(compressed.data(), compressed_size);
        if (in.gcount() < compressed_size) {
            std::cerr << "❌ Incomplete block.\n";
            return false;
        }

        std::vector<char> block;
        bool ok = zlib_decompress_block(compressed, uncompressed_size, block, dict);
        if (!ok) {
            std::cerr << "❌ Block decompression failed: "
                      << "block #" << block_id
                      << ", comp=" << compressed_size
                      << ", uncomp=" << uncompressed_size
                      << ", offset=" << in.tellg() << "\n";
            return false;
        }

        std::cerr << "📦 Block #" << block_id++
                  << ": lines=" << lines
                  << ", comp=" << compressed_size
                  << ", uncomp=" << uncompressed_size << "\n";

        const char* p = block.data();
        for (uint32_t i = 0; i < lines; ++i) {
            uint32_t file_id = read_u32_mem(p);
            uint32_t tpl_id = read_u32_mem(p);
            uint32_t var_count = read_u32_mem(p);

            std::vector<uint32_t> var_ids(var_count);
            for (uint32_t j = 0; j < var_count; ++j)
                var_ids[j] = read_u32_mem(p);

            const std::string& tpl = templates[tpl_id];
            std::string reconstructed;
            size_t last = 0, vi = 0;

            while (true) {
                size_t pos = tpl.find("<VAR>", last);
                if (pos == std::string::npos) {
                    reconstructed.append(tpl, last);
                    break;
                }
                reconstructed.append(tpl, last, pos - last);
                if (vi < var_ids.size() && var_ids[vi] < variables.size()) {
                    reconstructed += variables[var_ids[vi]];
                    switch (types[var_ids[vi]]) {
                        case VarType::IP: ip_count++; break;
                        case VarType::TS: ts_count++; break;
                        case VarType::NUM: num_count++; break;
                    }
                } else {
                    reconstructed += "???";
                }
                last = pos + 5;
                vi++;
            }

            if (file_id < out_streams.size()) {
                out_streams[file_id] << reconstructed << "\n";
            } else {
                out_streams[0] << reconstructed << "\n";
            }

            total_lines++;
        }
    }

    for (auto& f : out_streams) f.close();

    std::cout << "✅ Decompressed " << total_lines << " lines into " << filenames.size() << " files.\n";
    std::cout << "📊 Variable usage:\n";
    std::cout << "   IPs: " << ip_count << "\n";
    std::cout << "   TS : " << ts_count << "\n";
    std::cout << "   NUM: " << num_count << "\n";
    std::cout << "🧠 Dict Size: " << dict.size() << " bytes (saved to decompression.dict)\n";

    return true;
}
