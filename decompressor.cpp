#include "decompressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <zlib.h>
#include <cstring>
#include <algorithm>

static uint32_t read_u32(std::ifstream &in)
{
    uint32_t v;
    in.read(reinterpret_cast<char *>(&v), 4);
    return v;
}

static uint32_t read_u32_mem(const char *&p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    p += 4;
    return v;
}

static bool zlib_decompress_block(const std::vector<char> &in_data,
                                  size_t uncompressed_size,
                                  std::vector<char> &out_data,
                                  const std::string &dict)
{
    z_stream strm{};
    if (inflateInit2(&strm, 15) != Z_OK)
        return false;

    out_data.resize(uncompressed_size);
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in_data.data()));
    strm.avail_in = static_cast<uInt>(in_data.size());
    strm.next_out = reinterpret_cast<Bytef *>(out_data.data());
    strm.avail_out = static_cast<uInt>(out_data.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_NEED_DICT && !dict.empty())
    {
        if (inflateSetDictionary(&strm, reinterpret_cast<const Bytef *>(dict.data()), dict.size()) != Z_OK)
        {
            inflateEnd(&strm);
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    }

    bool ok = (ret == Z_STREAM_END && strm.total_out == uncompressed_size);
    inflateEnd(&strm);
    return ok;
}

// Utility to read uint32_t from buffer
static uint32_t read_u32(const std::vector<char> &buf, size_t &offset)
{
    uint32_t v;
    std::memcpy(&v, buf.data() + offset, sizeof(uint32_t));
    offset += 4;
    return v;
}

// Reconstruct original line from template and vars
static std::string reconstruct_line(const std::string &tpl,
                                    const std::vector<std::string> &vars)
{
    std::ostringstream os;
    size_t start = 0, idx = 0;
    const std::string placeholder = "<VAR>";
    while (true)
    {
        size_t pos = tpl.find(placeholder, start);
        if (pos == std::string::npos)
            break;
        os << tpl.substr(start, pos - start);
        os << vars[idx++];
        start = pos + placeholder.size();
    }
    os << tpl.substr(start);
    return os.str();
}

void decompress_comp_to_blk(const std::vector<char> &comp,
                            std::vector<char> &blk,
                            uLong origSize)
{
    uLong destLen = origSize; // the expected size after decompression
    blk.resize(destLen);

    int ret = uncompress(
        reinterpret_cast<Bytef *>(blk.data()),        // destination buffer
        &destLen,                                     // in/out: will be updated to actual size
        reinterpret_cast<const Bytef *>(comp.data()), // source buffer
        static_cast<uLong>(comp.size())               // source size
    );

    if (ret != Z_OK)
    {
        throw std::runtime_error("zlib uncompress failed: " + std::to_string(ret));
    }

    // In the unlikely event destLen < origSize, shrink to the true size:
    blk.resize(destLen);
}

bool decompress_files_template_zlib(const std::string &archive_path,
                                    const std::string &output_folder)
{
    std::vector<std::string> templates, variables, files;
    std::unordered_map<uint32_t, std::string> tpl_map, var_map, file_map;

    if (!load_templates_and_variables(nullptr, templates, variables, files, tpl_map, var_map, file_map))
    {
        std::cerr << "❌ Failed to load dictionaries.json from \n";
        return false;
    }

    // Build dictionary string for zlib
    std::string dict;
    std::vector<std::string> sorted;
    sorted.reserve(tpl_map.size());
    for (const auto &s : tpl_map)
        sorted.push_back(s.second);
    std::sort(sorted.begin(), sorted.end());
    for (const auto &s : sorted)
        dict += s;
    sorted.clear();

    sorted.reserve(var_map.size());
    for (const auto &s : var_map)
        sorted.push_back(s.second);
    std::sort(sorted.begin(), sorted.end());
    for (const auto &s : sorted)
        dict += s;
    sorted.clear();

    sorted.reserve(file_map.size());
    for (const auto &s : file_map)
        sorted.push_back(s.second);
    std::sort(sorted.begin(), sorted.end());
    for (const auto &s : sorted)
        dict += s;
    sorted.clear();

    std::ofstream debug_dict("decompression.dict");
    debug_dict << dict;
    debug_dict.close();

    // Open archive
    std::ifstream in(archive_path, std::ios::binary);
    if (!in)
    {
        std::cerr << "❌ Failed to open archive: " << archive_path << "\n";
        return false;
    }

    // Read and verify header
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "TCDZ", 4) != 0)
    {
        std::cerr << "❌ Invalid archive format\n";
        return false;
    }

    // Read metadata
    uint32_t lines_val = 0, blk_size = 0, comp_size = 0;
    in.read(reinterpret_cast<char *>(&lines_val), 4);
    in.read(reinterpret_cast<char *>(&blk_size), 4);
    in.read(reinterpret_cast<char *>(&comp_size), 4);
    // comp_size = 4918386;
    std::cout << lines_val << ":" << blk_size << ":" << comp_size << std::endl;

    // Read compressed block
    std::vector<char> comp(comp_size);
    in.read(comp.data(), comp_size);

    // Decompress block
    std::vector<char> blk;

    decompress_comp_to_blk(comp, blk, blk_size);
    // if (!zlib_decompress_block(comp, blk_size, blk, dict))
    // {
    //     std::cerr << "❌ Decompression failed\n";
    //     return false;
    // }
    std::cout << "decompression done" << std::endl;

    // Parse block entries
    size_t offset = 0;
    struct Entry
    {
        uint32_t file_id, tpl_id, var_cnt;
        std::vector<uint32_t> var_ids;
    };

    std::ofstream debug_file("parsed_decompress.txt");
    std::vector<Entry> entries;
    int lineno = 0;
    while (lineno < lines_val)
    {
        Entry e;
        e.file_id = read_u32(blk, offset);
        e.tpl_id = read_u32(blk, offset);
        e.var_cnt = read_u32(blk, offset);

        debug_file << "file=" << e.file_id << " tpl=" << e.tpl_id
                   << " vars[" << e.var_cnt << "] = { ";

        for (uint32_t i = 0; i < e.var_cnt; ++i)
        {
            e.var_ids.push_back(read_u32(blk, offset));

            debug_file << e.var_ids[i] << (i + 1 == e.var_cnt ? " " : ", ");
        }
        entries.push_back(e);

        lineno++;

        debug_file << "}" << std::endl;
    }

    debug_file.close();

    std::cout << "parsing done" << std::endl;

    std::unordered_map<std::string, std::vector<std::string>> file_lines;
    for (const auto &e : entries)
    {
        std::vector<std::string> var_vals;
        for (uint32_t vid : e.var_ids)
            var_vals.push_back(var_map[vid]);
        std::string line = reconstruct_line(tpl_map[e.tpl_id], var_vals);
        file_lines[file_map[e.file_id]].push_back(line);
    }

    // Write out to files
    std::filesystem::create_directories(output_folder);
    for (const auto &kv : file_lines)
    {
        std::filesystem::path out_path = std::filesystem::path(output_folder) / kv.first;
        auto parent = out_path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        std::ofstream fout(out_path);
        if (!fout)
        {
            std::cerr << "❌ Cannot write to " << out_path << "\n";
            continue;
        }
        for (const auto &l : kv.second)
            fout << l << "\n";
    }
    return true;
}