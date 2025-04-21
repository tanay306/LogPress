#include "decompressor.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>
#include <zlib.h>
#include <cstdint>

// We'll read "TMZL" -> read uncompressed_size, compressed_size, read the zlib data
// => inflate => get "TMPL" data => parse => write files

static bool zlib_decompress_buffer(const std::vector<char>& in_data,
                                   size_t uncompressed_size,
                                   std::vector<char>& out_data)
{
    // We'll assume we know the exact uncompressed_size from the archive
    out_data.resize(uncompressed_size);

    uLongf dest_len = (uLongf)uncompressed_size;
    int ret = ::uncompress(
        reinterpret_cast<Bytef*>(out_data.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(in_data.data()),
        (uLong)in_data.size()
    );
    if (ret != Z_OK) {
        std::cerr << "zlib uncompress failed with code=" << ret << "\n";
        return false;
    }
    // sanity
    if (dest_len != uncompressed_size) {
        std::cerr << "zlib uncompress mismatch size.\n";
        return false;
    }
    return true;
}

// reads a 32-bit LE integer from a buffer
static uint32_t read_u32(const char* &p) {
    uint32_t val;
    std::memcpy(&val, p, 4);
    p += 4;
    return val;
}

bool decompress_files_template_zlib(const std::string& archive_path,
                                    const std::string& output_folder)
{
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Cannot open " << archive_path << "\n";
        return false;
    }

    char Z_magic[4];
    in.read(Z_magic, 4);
    if (in.gcount() < 4 || Z_magic[0] != 'T' || Z_magic[1] != 'M' ||
        Z_magic[2] != 'Z' || Z_magic[3] != 'L')
    {
        std::cerr << "Not a valid TMZL archive.\n";
        return false;
    }

    uint32_t unc_size = 0, cmp_size = 0;
    in.read(reinterpret_cast<char*>(&unc_size), 4);
    in.read(reinterpret_cast<char*>(&cmp_size), 4);
    if (!in.good()) {
        std::cerr << "Error reading uncompressed/compressed size.\n";
        return false;
    }

    // read compressed data
    std::vector<char> cmp_data(cmp_size);
    in.read(cmp_data.data(), cmp_size);
    if ((uint32_t)in.gcount() < cmp_size) {
        std::cerr << "Not enough data in archive.\n";
        return false;
    }
    in.close();

    // inflate
    std::vector<char> full_data;
    if (!zlib_decompress_buffer(cmp_data, unc_size, full_data)) {
        return false;
    }

    // parse "TMPL"
    const char *p = full_data.data();
    if (p[0] != 'T' || p[1] != 'M' || p[2] != 'P' || p[3] != 'L') {
        std::cerr << "Missing TMPL magic inside inflated data.\n";
        return false;
    }
    p += 4;

    // read template_count, line_count
    uint32_t template_count = read_u32(p);
    uint32_t line_count     = read_u32(p);

    // read templates
    std::vector<std::string> templates(template_count);
    for (uint32_t i = 0; i < template_count; i++) {
        uint32_t len = read_u32(p);
        templates[i].assign(p, p + len);
        p += len;
    }

    // read lines
    struct Line {
        uint32_t file_id;
        uint32_t template_id;
        std::vector<std::string> vars;
    };
    std::vector<Line> lines(line_count);
    for (uint32_t i = 0; i < line_count; i++) {
        lines[i].file_id     = read_u32(p);
        lines[i].template_id = read_u32(p);
        uint32_t var_count   = read_u32(p);
        lines[i].vars.resize(var_count);
        for (uint32_t v = 0; v < var_count; v++) {
            uint32_t vlen = read_u32(p);
            lines[i].vars[v].assign(p, p + vlen);
            p += vlen;
        }
    }

    // read filenames
    uint32_t file_count = read_u32(p);
    std::vector<std::string> filenames(file_count);
    for (uint32_t f = 0; f < file_count; f++) {
        uint32_t flen = read_u32(p);
        filenames[f].assign(p, p + flen);
        p += flen;
    }

    // reconstruct lines
    std::filesystem::create_directories(output_folder);
    std::vector<std::ofstream> outs(file_count);
    for (uint32_t f = 0; f < file_count; f++) {
        auto out_path = std::filesystem::path(output_folder) /
                        std::filesystem::path(filenames[f]).filename();
        outs[f].open(out_path.string());
        if (!outs[f].is_open()) {
            std::cerr << "Cannot create " << out_path.string() << "\n";
            return false;
        }
    }

    // For each line, reconstruct by substituting <VAR> placeholders
    for (auto &ln : lines) {
        if (ln.template_id >= template_count) {
            std::cerr << "Bad template_id.\n";
            return false;
        }
        if (ln.file_id >= file_count) {
            std::cerr << "Bad file_id.\n";
            return false;
        }
        std::string const& tpl = templates[ln.template_id];
        std::string reconstructed;
        reconstructed.reserve(tpl.size() + 10*ln.vars.size());
        size_t start = 0, var_i = 0;
        while (true) {
            size_t pos = tpl.find("<VAR>", start);
            if (pos == std::string::npos) {
                reconstructed.append(tpl, start, tpl.size() - start);
                break;
            }
            reconstructed.append(tpl, start, pos - start);
            if (var_i < ln.vars.size()) {
                reconstructed += ln.vars[var_i++];
            } else {
                reconstructed += "???";
            }
            start = pos + 5;
        }

        outs[ln.file_id] << reconstructed << "\n";
    }

    for (auto &ofs : outs) {
        ofs.close();
    }

    return true;
}