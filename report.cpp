#include <mysql.h>
#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

const char* DB_HOST = "localhost";
const char* DB_USER = "root";
const char* DB_PASS = "root"; // replace this
const char* DB_NAME = "activity_monitor";

MYSQL* ConnectDB() {
    MYSQL* conn = mysql_init(NULL);
    if (conn == NULL) {
        cerr << "mysql_init failed\n";
        return nullptr;
    }
    conn = mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 3306, NULL, 0);
    if (conn == NULL) {
        cerr << "Connection failed: " << mysql_error(conn) << "\n";
        return nullptr;
    }
    return conn;
}

// Runs a query and prints results as a simple table
void RunAndPrint(MYSQL* conn, const string& title, const string& query) {
    cout << "\n=== " << title << " ===\n";

    if (mysql_query(conn, query.c_str())) {
        cerr << "Query failed: " << mysql_error(conn) << "\n";
        return;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (result == NULL) {
        cerr << "Failed to fetch result: " << mysql_error(conn) << "\n";
        return;
    }

    int numFields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // Print header
    for (int i = 0; i < numFields; i++) {
        cout << left << setw(20) << fields[i].name;
    }
    cout << "\n";
    for (int i = 0; i < numFields; i++) {
        cout << "--------------------";
    }
    cout << "\n";

    // Print rows
    MYSQL_ROW row;
    int rowCount = 0;
    while ((row = mysql_fetch_row(result))) {
        for (int i = 0; i < numFields; i++) {
            cout << left << setw(20) << (row[i] ? row[i] : "NULL");
        }
        cout << "\n";
        rowCount++;
    }

    if (rowCount == 0) {
        cout << "(no rows)\n";
    }

    mysql_free_result(result);
}

int main() {
    MYSQL* conn = ConnectDB();
    if (conn == nullptr) return 1;

    cout << "ACTIVITY MONITOR - REPORT\n";
    cout << "=========================\n";

    RunAndPrint(conn, "Session Overview",
        "SELECT session_id, start_time, end_time, "
        "TIMESTAMPDIFF(SECOND, start_time, end_time) AS duration_sec, "
        "total_idle_seconds, "
        "ROUND((total_idle_seconds / NULLIF(TIMESTAMPDIFF(SECOND, start_time, end_time), 0)) * 100, 2) AS idle_pct "
        "FROM sessions WHERE end_time IS NOT NULL ORDER BY session_id");

    RunAndPrint(conn, "Most-Used Applications",
        "SELECT process_name, COUNT(*) AS times_seen, "
        "ROUND(AVG(cpu_usage), 2) AS avg_cpu, "
        "ROUND(AVG(memory_usage_mb), 2) AS avg_memory_mb "
        "FROM process_snapshots GROUP BY process_name ORDER BY times_seen DESC");

    RunAndPrint(conn, "Highest CPU Usage Per Session",
        "SELECT ps.session_id, ps.process_name, ps.cpu_usage, ps.log_time "
        "FROM process_snapshots ps "
        "INNER JOIN (SELECT session_id, MAX(cpu_usage) AS max_cpu FROM process_snapshots GROUP BY session_id) top "
        "ON ps.session_id = top.session_id AND ps.cpu_usage = top.max_cpu "
        "ORDER BY ps.session_id");

    RunAndPrint(conn, "Active vs Idle Snapshot Counts",
        "SELECT s.session_id, "
        "COUNT(CASE WHEN ps.is_idle = 1 THEN 1 END) AS idle_snapshots, "
        "COUNT(CASE WHEN ps.is_idle = 0 THEN 1 END) AS active_snapshots, "
        "COUNT(*) AS total_snapshots "
        "FROM sessions s JOIN process_snapshots ps ON s.session_id = ps.session_id "
        "GROUP BY s.session_id ORDER BY s.session_id");

    mysql_close(conn);
    return 0;
}