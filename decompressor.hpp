#ifndef TEMPLATE_DECOMPRESSOR_HPP
#define TEMPLATE_DECOMPRESSOR_HPP

#include <string>

/**
 * Decompress a "template+zlib" archive, restoring the original logs into
 * an output folder. 
 */
bool decompress_files_template_zlib(const std::string& archive_path,
                                    const std::string& output_folder);

#endif // TEMPLATE_DECOMPRESSOR_HPP
