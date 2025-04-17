#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <queue>

#include "compressor.hpp"
#include "decompressor.hpp"
#include "searcher.hpp"

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
        std::queue<std::string> paths;
        for (int i = 3; i < argc; i++)
        {
            paths.push(argv[i]);
        }
        while (!paths.empty())
        {
            auto path = paths.front();
            paths.pop();
            if (std::filesystem::is_directory(path))
            {
                for (const auto &entry : std::filesystem::directory_iterator(path))
                {
                    paths.push(entry.path());
                }
            }
            else
            {
                input_paths.push_back(path);
            }
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
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    return 0;
}
