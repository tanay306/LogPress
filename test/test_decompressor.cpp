#ifndef _Alignof
#define _Alignof(x) __alignof__(x)
#endif

#include "decompressor.hpp"
#include "sqlite_helper.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

// Dump dictionary to file
void save_dict(const std::string& filename,
               const std::vector<std::string>& templates,
               const std::vector<std::string>& variables,
               const std::vector<std::string>& filenames) {
    std::ofstream out(filename);
    for (const auto& s : templates) out << s;
    for (const auto& v : variables) out << v;
    for (const auto& f : filenames) out << f;
    out.close();
}

int main() {
    std::string archive_path = "test.tcdb";
    std::string output_folder = "test_output";

    // Load .meta.db dictionary
    sqlite3* db = nullptr;
    std::vector<std::string> templates, variables, filenames;
    std::vector<VarType> types;

    std::filesystem::path meta_path = std::filesystem::absolute(archive_path + ".meta.db");
    std::cout << "📂 Opening meta.db at: " << meta_path << "\n";

    
    if (sqlite3_open(meta_path.string().c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open existing meta.db: "
                  << archive_path + ".meta.db" << "\n";
        return 1;
    }
    if (!load_templates_and_variables(db, templates, variables, types, filenames)) {
        std::cerr << "❌ Failed to load from meta.db\n";
        return 1;
    }
    sqlite3_close(db);

    std::cout << "✅ Loaded " << templates.size() << " templates, "
              << variables.size() << " variables.\n";

    save_dict("decompression.dict", templates, variables, filenames);
    std::cout << "📄 Dictionary saved to decompression.dict\n";

    // Decompress and write to file
    if (!decompress_files_template_zlib(archive_path, output_folder)) {
        std::cerr << "❌ Decompression failed.\n";
        return 1;
    }

    std::cout << "✅ Decompression complete. Output in: " << output_folder << "\n";
    std::cout << "Compare with expected using:\n";
    std::cout << "  diff compression.dict decompression.dict\n";

    return 0;
}
