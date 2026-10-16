#include <mysql.h>
#include <iostream>
#include <fstream>
#include <map>
#include <string>

std::map<std::string, std::string> loadConfig(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);
    std::string line;
    std::string section;

    if (!file.is_open()) {
        std::cerr << "Could not open config file: " << filename << "\n";
        return config;
    }

    while (std::getline(file, line)) {
        const std::size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        line = line.substr(start);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[') {
            const std::size_t end = line.find(']');
            if (end != std::string::npos) {
                section = line.substr(1, end - 1);
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        const std::size_t valueEnd = value.find_last_not_of(" \t\r\n");
        if (valueEnd != std::string::npos) {
            value.resize(valueEnd + 1);
        }
        config[section + "." + key] = value;
    }

    return config;
}

int main() {
    const std::map<std::string, std::string> config = loadConfig("config.ini");
    const auto host = config.find("database.host");
    const auto user = config.find("database.user");
    const auto password = config.find("database.password");
    const auto database = config.find("database.database");

    if (host == config.end() || user == config.end() ||
        password == config.end() || database == config.end() ||
        host->second.empty() || user->second.empty() ||
        database->second.empty()) {
        std::cerr << "config.ini is missing database settings\n";
        return 1;
    }

    MYSQL* conn = mysql_init(NULL);

    if (conn == NULL) {
        std::cerr << "mysql_init failed\n";
        return 1;
    }

    conn = mysql_real_connect(
        conn,
        host->second.c_str(),
        user->second.c_str(),
        password->second.c_str(),
        database->second.c_str(),
        3306,
        NULL,
        0
    );

    if (conn == NULL) {
        std::cerr << "Connection failed: " << mysql_error(conn) << "\n";
        return 1;
    }

    std::cout << "Connected to activity_monitor DB successfully!\n";

    // Verify that the current schema is available without creating test data.
    const std::string query = "SELECT COUNT(*) FROM sessions";

    if (mysql_query(conn, query.c_str())) {
        std::cerr << "Schema check failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return 1;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (result == NULL) {
        std::cerr << "Could not read schema check result: "
                  << mysql_error(conn) << "\n";
        mysql_close(conn);
        return 1;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    std::cout << "Schema check passed. Sessions in database: "
              << (row != NULL && row[0] != NULL ? row[0] : "0") << "\n";
    mysql_free_result(result);

    mysql_close(conn);
    return 0;
}