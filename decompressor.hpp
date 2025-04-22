#pragma once

#include <string>
#include <cstdint>

// Entry point to decompress archive using SQLite + typed variable reconstruction
bool decompress_files_template_zlib(const std::string& archive_path,
                                    const std::string& output_folder);
