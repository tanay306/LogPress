#ifndef COMPRESSOR_HPP
#define COMPRESSOR_HPP

#include <string>
#include <vector>

// Compress the given log files into an archive.
// Optionally, specify the number of log lines per block (default 1000).
bool compress_files_template_zlib(const std::vector<std::string>& input_files,
                                  const std::string& archive_path,
                                  size_t lines_per_block = 5000);

#endif // COMPRESSOR_HPP
