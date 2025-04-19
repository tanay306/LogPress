#include "sqlite_helper.hpp"
#include <iostream>

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
    std::cout << "[SQLITE] Inserting "
              << templates.size() << " templates, "
              << variables.size() << " variables, "
              << files.size() << " files\n";

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM templates;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM variables;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM files;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;

    // Insert templates
    sqlite3_prepare_v2(db, "INSERT INTO templates (id, template) VALUES (?, ?);", -1, &stmt, nullptr);
    for (size_t i = 0; i < templates.size(); ++i) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, templates[i].c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "❌ Insert template failed: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    // Insert variables
    sqlite3_prepare_v2(db, "INSERT INTO variables (id, value) VALUES (?, ?);", -1, &stmt, nullptr);
    for (size_t i = 0; i < variables.size(); ++i) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, variables[i].c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "❌ Insert variable failed: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    // Insert files
    sqlite3_prepare_v2(db, "INSERT INTO files (id, filename) VALUES (?, ?);", -1, &stmt, nullptr);
    for (size_t i = 0; i < files.size(); ++i) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, files[i].c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "❌ Insert file failed: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    std::cout << "[SQLITE] Done writing meta.db ✅\n";
    return true;
}

bool load_templates_and_variables(sqlite3* db,
                                  std::vector<std::string>& templates,
                                  std::vector<std::string>& variables,
                                  std::vector<std::string>& files) {
    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(db, "SELECT template FROM templates ORDER BY id;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        templates.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT value FROM variables ORDER BY id;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        variables.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT filename FROM files ORDER BY id;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        files.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    std::cout << "[SQLITE] Loaded " << templates.size() << " templates, "
              << variables.size() << " variables, "
              << files.size() << " files ✅\n";

    return true;
}
