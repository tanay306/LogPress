#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include "compressor.hpp"

bool initialize_db(sqlite3*& db, const std::string& db_path);

bool store_templates_and_variables(sqlite3* db,
                                   const std::vector<std::string>& templates,
                                   const std::vector<std::string>& variables,
                                   const std::vector<std::string>& files);

bool load_templates_and_variables(sqlite3* db,
                                  std::vector<std::string>& templates,
                                  std::vector<std::string>& variables,
                                  std::vector<std::string>& files);
