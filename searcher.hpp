#pragma once

#include <string>

bool search_archive_template_zlib(const std::string& archive_path,
                                  const std::string& search_term,
                                  const std::string& type_filter = "");
