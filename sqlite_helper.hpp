#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <sqlite3.h>
#include "compressor.hpp"

bool initialize_db(sqlite3 *&db, const std::string &db_path);

bool store_templates_and_variables(sqlite3 *db,
                                   std::map<std::string, uint32_t> &tpl_map,
                                   std::map<std::string, uint32_t> &var_map,
                                   std::map<std::string, uint32_t> &file_map);

bool store_templates_and_variables2(sqlite3 *db,
                                    const std::vector<std::string> &templates,
                                    const std::vector<std::string> &variables,
                                    const std::vector<std::string> &files,
                                    std::unordered_map<std::string, uint32_t> &tpl_map,
                                    std::unordered_map<std::string, uint32_t> &var_map,
                                    std::unordered_map<std::string, uint32_t> &file_map);

bool load_templates_and_variables(sqlite3 *db,
                                  std::vector<std::string> &templates,
                                  std::vector<std::string> &variables,
                                  std::vector<std::string> &files,
                                  std::unordered_map<uint32_t, std::string> &tpl_map,
                                  std::unordered_map<uint32_t, std::string> &var_map,
                                  std::unordered_map<uint32_t, std::string> &file_map);
