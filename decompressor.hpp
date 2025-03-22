#ifndef DECOMPRESSOR_HPP
#define DECOMPRESSOR_HPP

#include <string>

// Decompress the archive into separate output files (restoring original log lines).
bool decompress_files_template_zlib(const std::string& archive_path,
                                    const std::string& output_folder);

#endif // DECOMPRESSOR_HPP
