#ifndef TEMPLATE_COMPRESSOR_HPP
#define TEMPLATE_COMPRESSOR_HPP

#include <string>
#include <vector>

/**
 * Compress multiple text files into a single "template-based" archive,
 * then do a zlib pass on the entire result for better compression.
 *
 * @param input_files    The log files to compress
 * @param archive_path   Output .myclp file
 * @return true on success
 */
bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path);

#endif // TEMPLATE_COMPRESSOR_HPP
