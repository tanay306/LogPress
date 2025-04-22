#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "compressor.hpp"
#include "decompressor.hpp"
#include "searcher.hpp"

std::vector<std::string> get_log_files(const std::string& path) {
    std::vector<std::string> files;
    std::filesystem::path p(path);
    if (std::filesystem::is_regular_file(p)) {
        files.push_back(p.string());
    } else if (std::filesystem::is_directory(p)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(p)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

void save_logs_to_single_file(const std::string& path, const std::string& output_file) {
    std::vector<std::string> log_files = get_log_files(path);
    
    std::ofstream out(output_file, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << output_file << std::endl;
        return;
    }

    for (const auto& file : log_files) {
        std::ifstream in(file);
        if (in.is_open()) {
            out << in.rdbuf();
            in.close();
        } else {
            std::cerr << "Failed to open log file: " << file << std::endl;
        }
    }

    out.close();
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " compress   <archive> <file1> [file2 ...]\n"
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
        for (int i = 3; i < argc; i++) {
            input_paths.push_back(argv[i]);
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
            std::cerr << "Not enough arguments for search.\n";
            return 1;
        }
        std::string archive_path = argv[2];
        std::string search_term  = argv[3];
        if (!search_archive_template_zlib(archive_path, search_term)) {
            std::cerr << "Search failed.\n";
            return 1;
        }
    }
    else if (command == "get") {
        std::string archive_path = argv[2];
        std::string output_file = "combined_logs.txt";        // Replace with desired output file path
        save_logs_to_single_file(archive_path, output_file);
        std::cout << "Logs have been saved to " << output_file << std::endl;
    }
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    return 0;
}
