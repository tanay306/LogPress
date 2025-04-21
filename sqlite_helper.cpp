#include "sqlite_helper.hpp"
#include <iostream>
#include <fstream>
#include "external/json.hpp"
#include <curl/curl.h>

using json = nlohmann::json;

bool initialize_db(sqlite3 *&db, const std::string &db_path)
{
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "❌ DB Open Error: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    const char *schema = R"(
        DROP TABLE IF EXISTS templates;
        DROP TABLE IF EXISTS variables;
        DROP TABLE IF EXISTS files;

        CREATE TABLE templates (id INTEGER PRIMARY KEY, template TEXT);
        CREATE TABLE variables (id INTEGER PRIMARY KEY, value TEXT);
        CREATE TABLE files (id INTEGER PRIMARY KEY, filename TEXT);
    )";
    char *errMsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        std::cerr << "❌ DB Schema Error: " << errMsg << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool store_templates_and_variables2(sqlite3 *db,
                                    const std::vector<std::string> &templates,
                                    const std::vector<std::string> &variables,
                                    const std::vector<std::string> &files,
                                    std::unordered_map<std::string, uint32_t> &tpl_map,
                                    std::unordered_map<std::string, uint32_t> &var_map,
                                    std::unordered_map<std::string, uint32_t> &file_map)
{
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

bool store_templates_and_variables(sqlite3 *db,
                                   std::map<std::string, uint32_t> &tpl_map,
                                   std::map<std::string, uint32_t> &var_map,
                                   std::map<std::string, uint32_t> &file_map)
{
    using namespace std;
    ifstream inFile("dictionaries.json");
    if (!inFile.is_open())
    {
        cerr << "Failed to open JSON file." << endl;
        return 1;
    }

    json j;
    inFile >> j;

    for (auto &[key, value] : j["templates"].items())
    {
        tpl_map[key] = static_cast<uint32_t>(value.get<int>());
        // std::cout << "t " << key << ":" << value << endl;
    }

    for (auto &[key, value] : j["variables"].items())
    {
        var_map[key] = static_cast<uint32_t>(value.get<int>());
        // std::cout << "v " << key << ":" << value << endl;
    }

    for (auto &[key, value] : j["files"].items())
    {
        file_map[key] = static_cast<uint32_t>(value.get<int>());
        // std::cout << "f " << key << ":" << value << endl;
    }
}

bool load_templates_and_variables(sqlite3 *db,
                                  std::vector<std::string> &templates,
                                  std::vector<std::string> &variables,
                                  std::vector<std::string> &files,
                                  std::unordered_map<uint32_t, std::string> &tpl_map,
                                  std::unordered_map<uint32_t, std::string> &var_map,
                                  std::unordered_map<uint32_t, std::string> &file_map)
{
    const std::string json_path = "dictionaries.json";

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
        // -- templates --
        templates.clear();
        tpl_map.clear();
        if (j.contains("templates"))
        {
            for (auto &el : j["templates"].items())
            {
                const auto &name = el.key();
                uint32_t id = el.value().get<uint32_t>();
                templates.push_back(name);
                tpl_map[id] = name;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Failed to parse JSON: " << e.what() << "\n";
        return false;
    }

    try
    {
        // -- variables --
        variables.clear();
        var_map.clear();
        if (j.contains("variables"))
        {
            for (auto &el : j["variables"].items())
            {
                const auto &value = el.key();
                uint32_t id = el.value().get<uint32_t>();
                variables.push_back(value);
                var_map[id] = value;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Failed to parse JSON: " << e.what() << "\n";
        return false;
    }

    try
    {
        // -- files --
        files.clear();
        file_map.clear();
        if (j.contains("files"))
        {
            for (auto &el : j["files"].items())
            {
                const auto &fname = el.key();
                uint32_t id = el.value().get<uint32_t>();
                files.push_back(fname);
                file_map[id] = fname;
            }
        }
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
