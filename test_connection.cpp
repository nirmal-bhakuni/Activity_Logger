#include <mysql.h>
#include <iostream>

int main() {
    MYSQL* conn = mysql_init(NULL);

    if (conn == NULL) {
        std::cerr << "mysql_init failed\n";
        return 1;
    }

    conn = mysql_real_connect(
        conn,
        "192.168.1.20",     // previously "localhost",
        "monitor_user",
        "ChooseAStrongPassword123!",   // same password as before
        "activity_monitor", // now connecting to the actual DB
        3306,
        NULL,
        0
    );

    if (conn == NULL) {
        std::cerr << "Connection failed: " << mysql_error(conn) << "\n";
        return 1;
    }

    std::cout << "Connected to activity_monitor DB successfully!\n";

    // Test insert
    std::string query = "INSERT INTO activity_log (process_name, cpu_usage, memory_usage_mb, is_idle) "
                         "VALUES ('test_process.exe', 12.5, 340.2, 0)";

    if (mysql_query(conn, query.c_str())) {
        std::cerr << "Insert failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return 1;
    }

    std::cout << "Test row inserted successfully!\n";

    mysql_close(conn);
    return 0;
}