#include "compressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <unordered_map>
#include <map>
#include <zlib.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <iomanip>
#include <unordered_set>

// Utility to write uint32_t
void write_u32(std::vector<char> &buf, uint32_t v)
{
    char tmp[4];
    std::memcpy(tmp, &v, 4);
    buf.insert(buf.end(), tmp, tmp + 4);
}

static std::regex g_var_regex(R"([+\-]?\d+(?:[._:\-]\d+)*)");

ParseResult make_typed_template(const std::string &line)
{
    ParseResult r;
    size_t last = 0;
    for (std::sregex_iterator it(line.begin(), line.end(), g_var_regex), end; it != end; ++it)
    {
        auto m = *it;
        r.tpl.append(line, last, m.position() - last);
        r.tpl += "<VAR>";
        r.vars.push_back(m.str());
        last = m.position() + m.length();
    }
    r.tpl.append(line, last);
    return r;
}

// ── Pretty‑print one encoded line (same as before) ─────────────────────────────
static std::string encoded_line_to_string(const char *&ptr)
{
    uint32_t file_id, tpl_id, var_cnt;
    std::memcpy(&file_id, ptr, 4);
    ptr += 4;
    std::memcpy(&tpl_id, ptr, 4);
    ptr += 4;
    std::memcpy(&var_cnt, ptr, 4);
    ptr += 4;

    std::ostringstream oss;
    oss << "file=" << file_id << " tpl=" << tpl_id
        << " vars[" << var_cnt << "] = { ";

    for (uint32_t i = 0; i < var_cnt; ++i)
    {
        uint32_t v;
        std::memcpy(&v, ptr, 4);
        ptr += 4;
        oss << v << (i + 1 == var_cnt ? " " : ", ");
    }
    oss << "}\n";
    return oss.str();
}

// Zlib compression
bool zlib_compress_block(const std::vector<char> &in, std::vector<char> &out, const std::string &dict)
{
    z_stream strm{};
    if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    deflateSetDictionary(&strm, reinterpret_cast<const Bytef *>(dict.data()), dict.size());
    out.resize(compressBound(in.size()));
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.data()));
    strm.avail_in = static_cast<uInt>(in.size());
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END)
    {
        deflateEnd(&strm);
        return false;
    }
    out.resize(strm.total_out);
    deflateEnd(&strm);
    return true;
}

void compress_blk_to_comp(const std::vector<char> &blk,
                          std::vector<char> &comp,
                          int level = Z_BEST_COMPRESSION)
{
    uLong srcLen = static_cast<uLong>(blk.size());
    uLong bound = compressBound(srcLen); // worst‑case size
    comp.resize(bound);

    int ret = compress2(
        reinterpret_cast<Bytef *>(comp.data()),      // dest
        &bound,                                      // in/out: compressed size
        reinterpret_cast<const Bytef *>(blk.data()), // source
        srcLen,                                      // source size
        level                                        // compression level
    );

    if (ret != Z_OK)
    {
        throw std::runtime_error("zlib compress2 failed: " + std::to_string(ret));
    }

    comp.resize(bound); // trim to actual compressed size
}

bool compress_files_template_zlib(const std::vector<std::string> &input_files,
                                  const std::string &archive_path,
                                  size_t lines_per_block)
{
    std::map<std::string, uint32_t> tpl_map, var_map, file_map;
    std::vector<std::string> templates, variables, files;
    std::vector<char> current_block;
    std::vector<std::vector<char>> blocks;
    std::vector<ParseResult> prs;
    size_t total_lines = 0;

    for (const auto &file : input_files)
    {
        std::ifstream in(file);
        if (!in)
        {
            std::cerr << "File open failed: " << file << "\n";
            return false;
        }

        uint32_t file_id = file_map.emplace(file, files.size()).first->second;
        if (file_id == files.size())
            files.push_back(file);

        std::string line;
        while (std::getline(in, line))
        {
            ParseResult pr = make_typed_template(line);
            pr.file = file;

            if (pr.tpl.empty())
                continue;

            uint32_t tpl_id = tpl_map.emplace(pr.tpl, templates.size()).first->second;
            if (tpl_id == templates.size())
                templates.push_back(pr.tpl);

            for (size_t i = 0; i < pr.vars.size(); ++i)
            {
                const std::string &v = pr.vars[i];
                uint32_t var_id = var_map.emplace(v, variables.size()).first->second;
                if (var_id == variables.size())
                {
                    variables.push_back(v);
                }
            }

            prs.push_back(pr);
            total_lines++;
        }
    }

    // std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    // Build meta database path: "db/<archive_filename>.meta.db"
    std::filesystem::path archive_p(archive_path);
    std::filesystem::path meta_path = "./db/" + (archive_p.filename().string() + ".meta.db");
    std::cout << "Done parsing templates for all files" << std::endl;

    // SQLite metadata
    sqlite3 *db = nullptr;
    // if (!initialize_db(db, meta_path.string()))
    // {
    //     std::cerr << "Failed to init SQLite.\n";
    //     return false;
    // }
    store_templates_and_variables(db, tpl_map, var_map, file_map);
    // sqlite3_close(db);

    std::cout << "Done parsing dictionaries.json" << std::endl;

    for (auto &pr : prs)
    {

        uint32_t file_id = file_map[pr.file];
        uint32_t tpl_id = tpl_map[pr.tpl];
        uint32_t var_cnt = pr.vars.size();
        write_u32(current_block, file_id);
        write_u32(current_block, tpl_id);
        write_u32(current_block, pr.vars.size());
        for (size_t i = 0; i < pr.vars.size(); ++i)
        {
            const std::string &v = pr.vars[i];
            uint32_t var_id = var_map[v];
            write_u32(current_block, var_id);
        }
    }

    std::cout << "Done writing to block" << std::endl;

    // // Build dictionary
    std::string dict;
    for (const auto &s : tpl_map)
        dict += s.first;
    for (const auto &v : var_map)
        dict += v.first;
    for (const auto &f : file_map)
        dict += f.first;

    std::ofstream debug_dict("compression.dict");
    debug_dict << dict;
    debug_dict.close();

    std::cout << "Done writing compression.dict" << std::endl;

    std::ofstream out(archive_path, std::ios::binary);
    if (!out)
        return false;
    /* size bookkeeping --------------------------------------------------------- */
    size_t archive_size_no_zlib = 4;   // starts with magic "TCDZ"
    size_t archive_size_with_zlib = 4; // ditto
    out.write("TCDZ", 4);

    // Create directory & one stream per source file
    // std::filesystem::create_directories("./debug_by_file");
    // std::vector<std::ofstream> dbg_streams(files.size());
    // for (size_t fid = 0; fid < files.size(); ++fid)
    // {
    //     std::filesystem::path p = "./debug_by_file/";
    //     p /= std::filesystem::path(files[fid]).filename();
    //     p += ".encoded.txt";
    //     dbg_streams[fid].open(p);
    // }

    // std::ofstream debug_file("parsed_compress.txt");

    // std::cout << "Done creating all outstreams" << std::endl;

    // size_t blk_idx = 0;
    auto &blk = current_block;
    // // for (auto &blk : blocks)
    // // {
    // const char *cursor = blk.data();
    // const char *end = blk.data() + blk.size();

    // Optional header line once per file per block (tracks which streams we touched)
    // std::unordered_set<uint32_t> touched;

    // while (cursor < end)
    // {
    //     const char *line_start = cursor; // save start to peek file_id
    //     uint32_t file_id;
    //     std::memcpy(&file_id, cursor, 4);

    //     // Print header "=== block N ===" only the first time this file appears in this block
    //     if (!touched.count(file_id))
    //     {
    //         // dbg_streams[file_id] << "\n=== block   ===\n";
    //         touched.insert(file_id);
    //     }

    //     // Actually format & append the line
    //     auto str = encoded_line_to_string(cursor);
    //     // dbg_streams[file_id] << str;
    //     debug_file << str;
    //     // cursor is updated in ecoded_line_to_string function.
    // }

    // debug_file.close();

    // // blk_idx++;

    std::cout << "Done writing parsed block" << std::endl;

    std::vector<char> comp;
    compress_blk_to_comp(blk, comp);
    // if (!zlib_compress_block(blk, comp, dict))
    // {
    //     std::cerr << "Block compression failed.\n";
    //     return false;
    // }

    std::cout << "zlib part done for blkid" << std::endl;

    uint32_t lines_val = static_cast<uint32_t>(total_lines);
    uint32_t blk_size = static_cast<uint32_t>(blk.size());
    uint32_t comp_size = static_cast<uint32_t>(comp.size());
    archive_size_no_zlib += 12 + blk_size;    // header + raw block
    archive_size_with_zlib += 12 + comp_size; // header + compressed block

    std::cout << lines_val << ":" << blk_size << ":" << comp_size << std::endl;
    out.write(reinterpret_cast<const char *>(&lines_val), 4);
    out.write(reinterpret_cast<const char *>(&blk_size), 4);
    out.write(reinterpret_cast<const char *>(&comp_size), 4);
    out.write(comp.data(), comp.size());

    std::cout << "writing to out part done:" << std::endl;
    // }
    // std::cout << "[DEBUG] Final: variables.size()=" << variables.size();

    std::cout << "📦 Compressed " << total_lines << " lines in " << blocks.size() << " blocks.\n";
    std::cout << "📊 Templates: " << templates.size()
              << ", Variables: " << variables.size()
              << ", Files: " << files.size() << "\n";
    std::cout << "🧠 Dict Size: " << dict.size() / (1000 * 1000) << " MB (saved to compression.dict)\n";
    // Verify file size on disk
    std::error_code ec;
    auto actual_fs_size = std::filesystem::file_size(archive_path, ec);

    std::cout << std::fixed << std::setprecision(2)
              << "\n📏 Archive size (NO  zlib): " << static_cast<double>(archive_size_no_zlib) / (1000 * 1000) << " mb\n"
              << "🗜️  Archive size (WITH zlib): " << static_cast<double>(archive_size_with_zlib) / (1000 * 1000) << " mb\n";
    return true;
}

bool compress_files_template_zlib2(const std::vector<std::string> &input_files,
                                   const std::string &archive_path,
                                   size_t lines_per_block)
{
    std::unordered_map<std::string, uint32_t> tpl_map, var_map, file_map;
    std::vector<std::string> templates, variables, files;
    std::vector<char> current_block;
    std::vector<std::vector<char>> blocks;
    std::vector<ParseResult> prs;
    size_t total_lines = 0;

    for (const auto &file : input_files)
    {
        std::ifstream in(file);
        if (!in)
        {
            std::cerr << "File open failed: " << file << "\n";
            return false;
        }

        uint32_t file_id = file_map.emplace(file, files.size()).first->second;
        if (file_id == files.size())
            files.push_back(file);

        std::string line;
        while (std::getline(in, line))
        {
            ParseResult pr = make_typed_template(line);
            pr.file = file;

            if (pr.tpl.empty())
                continue;

            uint32_t tpl_id = tpl_map.emplace(pr.tpl, templates.size()).first->second;
            if (tpl_id == templates.size())
                templates.push_back(pr.tpl);

            for (size_t i = 0; i < pr.vars.size(); ++i)
            {
                const std::string &v = pr.vars[i];
                uint32_t var_id = var_map.emplace(v, variables.size()).first->second;
                if (var_id == variables.size())
                {
                    variables.push_back(v);
                }
            }

            total_lines++;
            if (total_lines % lines_per_block == 0)
            {
                blocks.push_back(std::move(current_block));
                current_block.clear();
            }
            prs.push_back(pr);
        }
    }

    if (!current_block.empty())
    {
        blocks.push_back(std::move(current_block));
    }

    // std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    // Build meta database path: "db/<archive_filename>.meta.db"
    // std::filesystem::path archive_p(archive_path);
    // std::filesystem::path meta_path = "./db/" + (archive_p.filename().string() + ".meta.db");
    // std::cout << "📂 Opening meta.db at: " << meta_path << "\n";

    // SQLite metadata
    sqlite3 *db = nullptr;
    // if (!initialize_db(db, meta_path.string()))
    // {
    //     std::cerr << "Failed to init SQLite.\n";
    //     return false;
    // }
    store_templates_and_variables2(db, templates, variables, files, tpl_map, var_map, file_map);
    // sqlite3_close(db);
    return true;
}
