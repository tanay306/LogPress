#ifndef SEARCHER_HPP
#define SEARCHER_HPP

#include <string>

// Search the archive for a given term (supports wildcards '*' and '?').
// The search is performed block by block to avoid decompressing the entire archive.
bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term);

#endif // SEARCHER_HPP
