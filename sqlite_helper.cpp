#include "sqlite_helper.hpp"
#include <iostream>
#include <fstream>
#include "external/json.hpp"
using json = nlohmann::json;

bool initialize_db(sqlite3*& db, const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ DB Open Error: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    const char* schema = R"(
        DROP TABLE IF EXISTS templates;
        DROP TABLE IF EXISTS variables;
        DROP TABLE IF EXISTS files;

        CREATE TABLE templates (id INTEGER PRIMARY KEY, template TEXT);
        CREATE TABLE variables (id INTEGER PRIMARY KEY, value TEXT);
        CREATE TABLE files (id INTEGER PRIMARY KEY, filename TEXT);
    )";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "❌ DB Schema Error: " << errMsg << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool store_templates_and_variables(sqlite3* db,
                                   const std::vector<std::string>& templates,
                                   const std::vector<std::string>& variables,
                                   const std::vector<std::string>& files) {
    const std::string json_path = "variables.json";
    std::cout << "[JSON] Writing to " << json_path << " with "
              << templates.size() << " templates, "
              << variables.size() << " variables, "
              << files.size() << " files\n";

    json j;

    j["templates"] = templates;
    j["variables"] = variables;
    j["files"] = files;

    std::ofstream out(json_path);
    if (!out)
    {
        std::cerr << "❌ Failed to open JSON file for writing: " << json_path << "\n";
        return false;
    }

    out << j.dump(2); // pretty-print with 2-space indentation
    out.close();

    std::cout << "[JSON] Done writing to " << json_path << " ✅\n";
    return true;
}

bool load_templates_and_variables(sqlite3* db,
                                  std::vector<std::string>& templates,
                                  std::vector<std::string>& variables,
                                  std::vector<std::string>& files) {
    const std::string json_path = "variables.json";

       std::ifstream in(json_path);
       if (!in)
       {
           std::cerr << "❌ Failed to open JSON file: " << json_path << "\n";
           return false;
       }

    json j;
    in >> j;

    try
    {
        templates = j.at("templates").get<std::vector<std::string>>();
        variables = j.at("variables").get<std::vector<std::string>>();
        files = j.at("files").get<std::vector<std::string>>();
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Failed to parse JSON: " << e.what() << "\n";
        return false;
    }

    std::cout << "[JSON] Loaded " << templates.size() << " templates, "
              << variables.size() << " variables, "
              << files.size() << " files ✅\n";

    return true;
}
