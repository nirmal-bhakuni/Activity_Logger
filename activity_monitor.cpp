#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <mysql.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <iomanip>

using namespace std;

// ---------- CONFIG ----------
const int SNAPSHOT_INTERVAL_SECONDS = 5;
const int TOTAL_RUN_SECONDS = 120; // 2 minutes
const int IDLE_THRESHOLD_SECONDS = 60;
// -----------------------------

// Global pointer so the Ctrl+C handler can access the DB connection and session ID
MYSQL* g_conn = nullptr;
unsigned long long g_sessionId = 0;
int g_totalIdleSeconds = 0;

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        if (g_conn != nullptr && g_sessionId != 0) {
            string endQuery = "UPDATE sessions SET end_time = NOW(), total_idle_seconds = "
                + to_string(g_totalIdleSeconds) + " WHERE session_id = " + to_string(g_sessionId);
            mysql_query(g_conn, endQuery.c_str());
            cout << "\nSession " << g_sessionId << " closed early (interrupted). Idle time recorded: "
                 << g_totalIdleSeconds << "s\n";
            mysql_close(g_conn);
        }
        exit(0);
    }
    return TRUE;
}

// ---------- Config file loader ----------
map<string, string> LoadConfig(const string& filename) {
    map<string, string> config;
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "Could not open config file: " << filename << "\n";
        return config;
    }

    string line, currentSection;
    while (getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == string::npos) continue;
        line = line.substr(start);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            currentSection = line.substr(1, end - 1);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == string::npos) continue;

        string key = line.substr(0, eq);
        string value = line.substr(eq + 1);

        size_t vEnd = value.find_last_not_of(" \t\r\n");
        if (vEnd != string::npos) value = value.substr(0, vEnd + 1);

        config[currentSection + "." + key] = value;
    }

    return config;
}

// ---------- Window visibility check ----------
struct WindowCheckData {
    DWORD pid;
    bool hasWindow;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    WindowCheckData* data = (WindowCheckData*)lParam;
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == data->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        data->hasWindow = true;
        return FALSE;
    }
    return TRUE;
}

bool ProcessHasVisibleWindow(DWORD pid) {
    WindowCheckData data;
    data.pid = pid;
    data.hasWindow = false;
    EnumWindows(EnumWindowsCallback, (LPARAM)&data);
    return data.hasWindow;
}

// ---------- CPU / memory helpers ----------
ULONGLONG FileTimeToInt(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

double GetMemoryUsageMB(HANDLE hProcess) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
    return -1.0;
}

double GetCpuUsagePercent(HANDLE hProcess, DWORD intervalMs = 200) {
    FILETIME creationTime, exitTime, kernelTime1, userTime1, kernelTime2, userTime2;
    FILETIME sysIdle1, sysKernel1, sysUser1, sysIdle2, sysKernel2, sysUser2;

    if (!GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime1, &userTime1))
        return -1.0;
    GetSystemTimes(&sysIdle1, &sysKernel1, &sysUser1);

    Sleep(intervalMs);

    if (!GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime2, &userTime2))
        return -1.0;
    GetSystemTimes(&sysIdle2, &sysKernel2, &sysUser2);

    ULONGLONG procDelta = (FileTimeToInt(kernelTime2) + FileTimeToInt(userTime2)) -
                          (FileTimeToInt(kernelTime1) + FileTimeToInt(userTime1));
    ULONGLONG sysDelta = (FileTimeToInt(sysKernel2) + FileTimeToInt(sysUser2)) -
                         (FileTimeToInt(sysKernel1) + FileTimeToInt(sysUser1));

    if (sysDelta == 0) return 0.0;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    return (double(procDelta) / double(sysDelta)) * 100.0 * numCores;
}

double GetIdleTimeSeconds() {
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&lii)) return -1.0;
    DWORD currentTick = GetTickCount();
    return (currentTick - lii.dwTime) / 1000.0;
}

// ---------- Process enumeration ----------
string WideToNarrow(const wchar_t* wstr) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, NULL, NULL);
    return result;
}

struct ProcessInfo {
    DWORD pid;
    string name;
};

vector<ProcessInfo> GetUserFacingProcesses() {
    vector<ProcessInfo> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (ProcessHasVisibleWindow(entry.th32ProcessID)) {
                ProcessInfo info;
                info.pid = entry.th32ProcessID;
                info.name = WideToNarrow(entry.szExeFile);
                result.push_back(info);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

int main() {
    // ---------- Load config ----------
    map<string, string> config = LoadConfig("config.ini");

    string dbHost = config["database.host"];
    string dbUser = config["database.user"];
    string dbPass = config["database.password"];
    string dbName = config["database.database"];
    string employeeId = config["employee.employee_id"];
    string employeeName = config["employee.employee_name"];

    if (dbHost.empty() || dbUser.empty() || dbName.empty()) {
        cerr << "Config file missing or incomplete. Check config.ini\n";
        return 1;
    }

    char machineNameBuf[256];
    DWORD machineNameSize = sizeof(machineNameBuf);
    GetComputerNameA(machineNameBuf, &machineNameSize);
    string machineName(machineNameBuf);

    cout << "Employee: " << employeeName << " (" << employeeId << ")  Machine: " << machineName << "\n";

    // ---------- Connect to MySQL ----------
    MYSQL* conn = mysql_init(NULL);
    if (conn == NULL) {
        cerr << "mysql_init failed\n";
        return 1;
    }

    conn = mysql_real_connect(conn, dbHost.c_str(), dbUser.c_str(), dbPass.c_str(), dbName.c_str(), 3306, NULL, 0);
    if (conn == NULL) {
        cerr << "Connection failed: " << mysql_error(conn) << "\n";
        return 1;
    }
    cout << "Connected to database.\n";

    // ---------- Start a new session ----------
    string startQuery = "INSERT INTO sessions (start_time, employee_id, machine_name) VALUES (NOW(), '"
        + employeeId + "', '" + machineName + "')";

    if (mysql_query(conn, startQuery.c_str())) {
        cerr << "Failed to create session: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return 1;
    }
    unsigned long long sessionId = mysql_insert_id(conn);
    cout << "Started session ID: " << sessionId << "\n";

    g_conn = conn;
    g_sessionId = sessionId;
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // ---------- Monitoring loop ----------
    int elapsed = 0;
    int totalIdleSeconds = 0;

    while (elapsed < TOTAL_RUN_SECONDS) {
        double idleSeconds = GetIdleTimeSeconds();
        bool isIdle = (idleSeconds >= IDLE_THRESHOLD_SECONDS);

        if (isIdle) {
            totalIdleSeconds += SNAPSHOT_INTERVAL_SECONDS;
            g_totalIdleSeconds = totalIdleSeconds;
        }

        vector<ProcessInfo> processes = GetUserFacingProcesses();

        cout << "\n[t=" << elapsed << "s] Idle: " << idleSeconds << "s  |  Status: "
             << (isIdle ? "IDLE" : "ACTIVE") << "\n";

        for (const auto& proc : processes) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, proc.pid);
            if (hProcess == NULL) continue;

            double cpu = GetCpuUsagePercent(hProcess);
            double mem = GetMemoryUsageMB(hProcess);
            CloseHandle(hProcess);

            string procName = proc.name;

            cout << "  " << procName << " (PID " << proc.pid << ") - CPU: "
                 << fixed << setprecision(2) << cpu << "%  Mem: " << mem << " MB\n";

            string insertQuery = "INSERT INTO process_snapshots "
                "(session_id, process_name, pid, cpu_usage, memory_usage_mb, is_idle) VALUES ("
                + to_string(sessionId) + ", '"
                + procName + "', "
                + to_string(proc.pid) + ", "
                + to_string(cpu) + ", "
                + to_string(mem) + ", "
                + (isIdle ? "1" : "0") + ")";

            if (mysql_query(conn, insertQuery.c_str())) {
                cerr << "Insert failed: " << mysql_error(conn) << "\n";
            }
        }

        int sampleTimeSpent = processes.size() * 200;
        int remainingSleep = (SNAPSHOT_INTERVAL_SECONDS * 1000) - sampleTimeSpent;
        if (remainingSleep > 0) {
            Sleep(remainingSleep);
        }
        elapsed += SNAPSHOT_INTERVAL_SECONDS;
    }

    // ---------- Close out the session ----------
    string endQuery = "UPDATE sessions SET end_time = NOW(), total_idle_seconds = "
        + to_string(totalIdleSeconds) + " WHERE session_id = " + to_string(sessionId);

    if (mysql_query(conn, endQuery.c_str())) {
        cerr << "Failed to update session: " << mysql_error(conn) << "\n";
    }

    cout << "\nSession " << sessionId << " complete. Total idle time: " << totalIdleSeconds << "s\n";

    mysql_close(conn);
    return 0;
}