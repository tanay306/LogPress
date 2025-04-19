#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "compressor.hpp"
#include "decompressor.hpp"
#include "searcher.hpp"

// Function to recursively collect files from a directory.
// Optionally, you can filter files by extension (for example, ".log").
std::vector<std::string> get_log_files(const std::string& path) {
    std::vector<std::string> files;
    std::filesystem::path p(path);
    if (std::filesystem::is_regular_file(p)) {
        files.push_back(p.string());
    } else if (std::filesystem::is_directory(p)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(p)) {
            if (entry.is_regular_file()) {
                // Uncomment the following lines to filter only .log files:
                // if (entry.path().extension() == ".log")
                //     files.push_back(entry.path().string());
                // Otherwise, add all files:
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " compress   <archive> <file_or_directory1> [file_or_directory2 ...]\n"
                  << "  " << argv[0] << " decompress <archive> <output_folder>\n"
                  << "  " << argv[0] << " search     <archive> <search_term>\n";
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "compress") {
        if (argc < 4) {
            std::cerr << "Not enough arguments for compress.\n";
            return 1;
        }
        std::string archive_path = argv[2];
        std::vector<std::string> input_paths;
        // For each provided file_or_directory argument, collect files recursively if needed.
        for (int i = 3; i < argc; i++) {
            std::vector<std::string> collected = get_log_files(argv[i]);
            input_paths.insert(input_paths.end(), collected.begin(), collected.end());
        }
        if (input_paths.empty()) {
            std::cerr << "No files found to compress.\n";
            return 1;
        }
        if (!compress_files_template_zlib(input_paths, archive_path)) {
            std::cerr << "Compression failed.\n";
            return 1;
        }
        std::cout << "Compressed into: " << archive_path << "\n";
    }
    else if (command == "decompress") {
        if (argc < 4) {
            std::cerr << "Not enough arguments for decompress.\n";
            return 1;
        }
        std::string archive_path  = argv[2];
        std::string output_folder = argv[3];
        if (!decompress_files_template_zlib(archive_path, output_folder)) {
            std::cerr << "Decompression failed.\n";
            return 1;
        }
        std::cout << "Decompressed into: " << output_folder << "\n";
    }
    else if (command == "search") {
        if (argc < 4) {
            std::cerr << "Usage: search <archive> <search_term>\n";
            return 1;
        }
        std::string archive = argv[2];
        std::string term = argv[3];
        if (!search_archive_template_zlib(archive, term)) {
            std::cerr << "Search failed.\n";
            return 1;
        }
    }
    
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }
    
    return 0;
}
