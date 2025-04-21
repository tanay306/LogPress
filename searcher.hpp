#pragma once

#include <string>
#include <sqlite3.h>

bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term);
