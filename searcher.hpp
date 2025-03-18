#ifndef TEMPLATE_SEARCHER_HPP
#define TEMPLATE_SEARCHER_HPP

#include <string>

/**
 * Search a "TMZL" + "TMPL" archive for lines containing `search_term`.
 * Prints "filename: line" for each match.
 */
bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term);

#endif // TEMPLATE_SEARCHER_HPP
