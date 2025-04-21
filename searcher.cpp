#include "searcher.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>
#include <zlib.h>
#include <cstdint>

static bool zlib_decompress_buffer(const std::vector<char>& in_data,
                                   size_t uncompressed_size,
                                   std::vector<char>& out_data)
{
    out_data.resize(uncompressed_size);
    uLongf dest_len = (uLongf)uncompressed_size;
    int ret = ::uncompress(
        reinterpret_cast<Bytef*>(out_data.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(in_data.data()),
        (uLong)in_data.size()
    );
    if (ret != Z_OK) {
        std::cerr << "zlib uncompress failed, code=" << ret << "\n";
        return false;
    }
    if (dest_len != uncompressed_size) {
        std::cerr << "mismatch uncompressed size.\n";
        return false;
    }
    return true;
}
static uint32_t read_u32(const char* &p) {
    uint32_t val;
    std::memcpy(&val, p, 4);
    p += 4;
    return val;
}

bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term)
{
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Cannot open " << archive_path << "\n";
        return false;
    }

    char Z_magic[4];
    in.read(Z_magic, 4);
    if (in.gcount() < 4 || Z_magic[0] != 'T' || Z_magic[1] != 'M' ||
        Z_magic[2] != 'Z' || Z_magic[3] != 'L') {
        std::cerr << "Not a valid TMZL file.\n";
        return false;
    }

    uint32_t unc_size = 0, cmp_size = 0;
    in.read(reinterpret_cast<char*>(&unc_size), 4);
    in.read(reinterpret_cast<char*>(&cmp_size), 4);
    if (!in.good()) {
        std::cerr << "Error reading sizes.\n";
        return false;
    }

    std::vector<char> cmp_data(cmp_size);
    in.read(cmp_data.data(), cmp_size);
    if ((uint32_t)in.gcount() < cmp_size) {
        std::cerr << "Not enough data.\n";
        return false;
    }
    in.close();

    // inflate
    std::vector<char> full_data;
    if (!zlib_decompress_buffer(cmp_data, unc_size, full_data)) {
        return false;
    }
    // parse
    const char* p = full_data.data();
    if (p[0] != 'T' || p[1] != 'M' || p[2] != 'P' || p[3] != 'L') {
        std::cerr << "Missing TMPL magic.\n";
        return false;
    }
    p += 4;

    uint32_t template_count = read_u32(p);
    uint32_t line_count     = read_u32(p);

    // read templates
    std::vector<std::string> templates(template_count);
    for (uint32_t i = 0; i < template_count; i++) {
        uint32_t len = read_u32(p);
        templates[i].assign(p, p+len);
        p += len;
    }

    struct Line {
        uint32_t file_id;
        uint32_t tpl_id;
        std::vector<std::string> vars;
    };
    std::vector<Line> lines(line_count);
    for (uint32_t i = 0; i < line_count; i++) {
        lines[i].file_id = read_u32(p);
        lines[i].tpl_id  = read_u32(p);
        uint32_t var_count = read_u32(p);
        lines[i].vars.resize(var_count);
        for (uint32_t v = 0; v < var_count; v++) {
            uint32_t vlen = read_u32(p);
            lines[i].vars[v].assign(p, p+vlen);
            p += vlen;
        }
    }

    uint32_t file_count = read_u32(p);
    std::vector<std::string> filenames(file_count);
    for (uint32_t f = 0; f < file_count; f++) {
        uint32_t flen = read_u32(p);
        filenames[f].assign(p, p+flen);
        p += flen;
    }

    // Reconstruct lines, search for search_term
    if (search_term.empty()) {
        std::cout << "(Empty search) printing all lines.\n";
    }
    bool found_any = false;
    for (auto &ln : lines) {
        if (ln.tpl_id >= template_count) {
            std::cerr << "Bad template id.\n";
            return false;
        }
        std::string const& tpl = templates[ln.tpl_id];
        // Rebuild
        std::string line;
        line.reserve(tpl.size() + 10*ln.vars.size());
        size_t start = 0, var_i = 0;
        while (true) {
            size_t pos = tpl.find("<VAR>", start);
            if (pos == std::string::npos) {
                line.append(tpl, start, tpl.size()-start);
                break;
            }
            line.append(tpl, start, pos - start);
            if (var_i < ln.vars.size()) {
                line += ln.vars[var_i++];
            } else {
                line += "???";
            }
            start = pos + 5;
        }
        // check search
        if (search_term.empty() || line.find(search_term) != std::string::npos) {
            found_any = true;
            uint32_t f_id = ln.file_id;
            std::string fn = (f_id < file_count ? filenames[f_id] : "UNKNOWN");

            std::string cluster_id;
            size_t cluster_pos = line.find("|ClusterID:");
            if (cluster_pos != std::string::npos) {
                cluster_id = line.substr(cluster_pos);
                line.erase(cluster_pos); // Remove the Cluster ID from the line
            }

            // Print with formatting, including the cluster ID separately
            std::cout << std::setw(30) << std::left << fn // File name aligned
                      << " : " 
                      << std::setw(15) << std::left << cluster_id // Cluster ID aligned
                      << " : " << line << "\n";
        }
    }

    if (!found_any) {
        std::cout << "No matches found.\n";
    }

    return true;
}
