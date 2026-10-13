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
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;

// ---------- CONFIG ----------
const int SNAPSHOT_INTERVAL_SECONDS = 5;
const int TOTAL_RUN_SECONDS = 120; // 2 minutes
const int IDLE_THRESHOLD_SECONDS = 60;
const int CONSUMER_DEMO_DELAY_MS = 0; // demo only: set to 0 for real use
// -----------------------------

// ---------- Shared state ----------
atomic<int> g_totalIdleSeconds{0};   // written by producer, read by main after join
atomic<bool> g_stopRequested{false}; // set by Ctrl+C / close handler
atomic<bool> g_cleanupDone{false};   // set by main when the session is closed

struct Snapshot {
    string processName;
    DWORD pid;
    double cpuUsage;
    double memoryUsageMB;
    bool isIdle;
};

// Shared resource 1: the queue (protected by g_queueMutex)
queue<Snapshot> g_snapshotQueue;
mutex g_queueMutex;
condition_variable g_queueCondVar;
bool g_producerDone = false;          // always read/written under g_queueMutex
const size_t MAX_QUEUE_SIZE = 50;

// Shared resource 2: session statistics (protected by g_statsMutex)
struct SessionStats {
    int totalSnapshotsWritten = 0;
    int totalIdleCount = 0;
};
SessionStats g_stats;
mutex g_statsMutex;

// LOCK ORDER RULE: if a thread ever needs both locks, it must take
// g_queueMutex FIRST and g_statsMutex SECOND. Never the reverse.

// ---------- Shutdown handler ----------
// Runs on a separate thread created by Windows. It does NOT touch the database.
// It only asks the other threads to stop, then waits for main() to finish cleanup.
BOOL WINAPI ConsoleHandler(DWORD signal) {
    switch (signal) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            {
                // Set the flag while holding the lock so a waiting thread
                // cannot miss the wake-up.
                lock_guard<mutex> lock(g_queueMutex);
                g_stopRequested = true;
            }
            g_queueCondVar.notify_all();

            // Windows kills the process shortly after this handler returns,
            // so wait (up to 4 seconds) until main() has closed the session.
            for (int i = 0; i < 40 && !g_cleanupDone; i++) {
                Sleep(100);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

// Sleeps in small pieces so a stop request is noticed quickly
void InterruptibleSleepMs(int ms) {
    int waited = 0;
    while (waited < ms && !g_stopRequested) {
        Sleep(100);
        waited += 100;
    }
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

// ---------- Producer: scans processes, pushes snapshots into the shared queue ----------
void ProducerThread() {
    int elapsed = 0;
    while (elapsed < TOTAL_RUN_SECONDS && !g_stopRequested) {
        double idleSeconds = GetIdleTimeSeconds();
        bool isIdle = (idleSeconds >= IDLE_THRESHOLD_SECONDS);

        if (isIdle) {
            g_totalIdleSeconds += SNAPSHOT_INTERVAL_SECONDS; // atomic
        }

        vector<ProcessInfo> processes = GetUserFacingProcesses();

        cout << "\n[Producer] [t=" << elapsed << "s] Idle: " << idleSeconds << "s  |  Status: "
             << (isIdle ? "IDLE" : "ACTIVE") << "\n";

        for (const auto& proc : processes) {
            if (g_stopRequested) break;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, proc.pid);
            if (hProcess == NULL) continue;

            double cpu = GetCpuUsagePercent(hProcess);
            double mem = GetMemoryUsageMB(hProcess);
            CloseHandle(hProcess);

            Snapshot snap;
            snap.processName = proc.name;
            snap.pid = proc.pid;
            snap.cpuUsage = cpu;
            snap.memoryUsageMB = mem;
            snap.isIdle = isIdle;

            // ---- Critical section: lock before touching the shared queue ----
            {
                unique_lock<mutex> lock(g_queueMutex);
                g_queueCondVar.wait(lock, [] {
                    return g_snapshotQueue.size() < MAX_QUEUE_SIZE || g_stopRequested;
                });
                if (g_stopRequested) break;
                g_snapshotQueue.push(snap);
            }
            g_queueCondVar.notify_one();
        }

        int sampleTimeSpent = processes.size() * 200;
        int remainingSleep = (SNAPSHOT_INTERVAL_SECONDS * 1000) - sampleTimeSpent;
        if (remainingSleep > 0) InterruptibleSleepMs(remainingSleep);
        elapsed += SNAPSHOT_INTERVAL_SECONDS;
    }

    {
        unique_lock<mutex> lock(g_queueMutex);
        g_producerDone = true;
    }
    g_queueCondVar.notify_all();
}

// ---------- Consumer: pulls snapshots from the queue, writes them to MySQL ----------
// This is the ONLY thread that uses the MySQL connection while the threads run.
void ConsumerThread(MYSQL* conn, unsigned long long sessionId) {
    while (true) {
        Snapshot snap;
        bool gotItem = false;

        {
            unique_lock<mutex> lock(g_queueMutex);
            g_queueCondVar.wait(lock, [] { return !g_snapshotQueue.empty() || g_producerDone; });

            if (!g_snapshotQueue.empty()) {
                snap = g_snapshotQueue.front();
                g_snapshotQueue.pop();
                gotItem = true;
            } else if (g_producerDone) {
                break;
            }
        }
        g_queueCondVar.notify_one();

        if (gotItem) {
            // Demo delay, skipped during shutdown so the queue drains quickly
            if (CONSUMER_DEMO_DELAY_MS > 0 && !g_stopRequested) {
                this_thread::sleep_for(chrono::milliseconds(CONSUMER_DEMO_DELAY_MS));
            }

            cout << "  [Consumer] Writing " << snap.processName << " (PID " << snap.pid << ")\n";

            string insertQuery = "INSERT INTO process_snapshots "
                "(session_id, process_name, pid, cpu_usage, memory_usage_mb, is_idle) VALUES ("
                + to_string(sessionId) + ", '"
                + snap.processName + "', "
                + to_string(snap.pid) + ", "
                + to_string(snap.cpuUsage) + ", "
                + to_string(snap.memoryUsageMB) + ", "
                + (snap.isIdle ? "1" : "0") + ")";

            if (mysql_query(conn, insertQuery.c_str())) {
                cerr << "Insert failed: " << mysql_error(conn) << "\n";
            }

            // ---- Update stats with its own lock (queue lock is NOT held here) ----
            {
                lock_guard<mutex> statsLock(g_statsMutex);
                g_stats.totalSnapshotsWritten++;
                if (snap.isIdle) {
                    g_stats.totalIdleCount++;
                }
            }
        }
    }
}

// ---------- Reporter: reads BOTH shared resources, always in the same lock order ----------
void ReporterThread() {
    int msSinceReport = 0;

    while (true) {
        this_thread::sleep_for(chrono::milliseconds(100));
        msSinceReport += 100;

        bool finished;
        size_t queueSize;
        int snapshotsWritten, idleCount;

        // Lock order: queue FIRST, then stats. Same order everywhere.
        {
            lock_guard<mutex> queueLock(g_queueMutex);
            lock_guard<mutex> statsLock(g_statsMutex);

            finished = g_producerDone && g_snapshotQueue.empty();
            queueSize = g_snapshotQueue.size();
            snapshotsWritten = g_stats.totalSnapshotsWritten;
            idleCount = g_stats.totalIdleCount;
        }

        if (finished) break;

        if (msSinceReport >= 3000) {
            cout << "  [Reporter] Queue size: " << queueSize
                 << "  |  Written so far: " << snapshotsWritten
                 << "  |  Idle count: " << idleCount << "\n";
            msSinceReport = 0;
        }
    }
}

int main() {
    cout.setf(ios::unitbuf); // auto-flush so run_log.txt is never empty

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

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // ---------- Start producer, consumer, and reporter threads ----------
    thread producer(ProducerThread);
    thread consumer(ConsumerThread, conn, sessionId);
    thread reporter(ReporterThread);

    producer.join();
    consumer.join();
    reporter.join();

    // ---------- Close out the session (only main uses the connection now) ----------
    int totalIdleSeconds = g_totalIdleSeconds.load();

    string endQuery = "UPDATE sessions SET end_time = NOW(), total_idle_seconds = "
        + to_string(totalIdleSeconds) + " WHERE session_id = " + to_string(sessionId);

    if (mysql_query(conn, endQuery.c_str())) {
        cerr << "Failed to update session: " << mysql_error(conn) << "\n";
    }

    if (g_stopRequested) {
        cout << "\nSession " << sessionId << " closed early (interrupted). Idle time recorded: "
             << totalIdleSeconds << "s\n";
    } else {
        cout << "\nSession " << sessionId << " complete. Total idle time: " << totalIdleSeconds << "s\n";
    }

    mysql_close(conn);
    g_cleanupDone = true; // lets the handler return
    return 0;
}