#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>

using namespace std;

// Converts a FILETIME to a single 64-bit integer for easy math
ULONGLONG FileTimeToInt(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Returns memory usage in MB for a given process handle
double GetMemoryUsageMB(HANDLE hProcess) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
    return -1.0;
}

// Returns CPU usage percentage for a given process, sampled over `intervalMs`
double GetCpuUsagePercent(HANDLE hProcess, DWORD intervalMs) {
    FILETIME creationTime, exitTime, kernelTime1, userTime1, kernelTime2, userTime2;
    FILETIME sysIdle1, sysKernel1, sysUser1, sysIdle2, sysKernel2, sysUser2;

    if (!GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime1, &userTime1))
        return -1.0;
    GetSystemTimes(&sysIdle1, &sysKernel1, &sysUser1);

    Sleep(intervalMs);

    if (!GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime2, &userTime2))
        return -1.0;
    GetSystemTimes(&sysIdle2, &sysKernel2, &sysUser2);

    ULONGLONG procTime1 = FileTimeToInt(kernelTime1) + FileTimeToInt(userTime1);
    ULONGLONG procTime2 = FileTimeToInt(kernelTime2) + FileTimeToInt(userTime2);
    ULONGLONG procDelta = procTime2 - procTime1;

    ULONGLONG sysTime1 = FileTimeToInt(sysKernel1) + FileTimeToInt(sysUser1);
    ULONGLONG sysTime2 = FileTimeToInt(sysKernel2) + FileTimeToInt(sysUser2);
    ULONGLONG sysDelta = sysTime2 - sysTime1;

    if (sysDelta == 0) return 0.0;

    // Multiply by number of CPU cores to get a 0-100% scale per process
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    double cpuPercent = (double(procDelta) / double(sysDelta)) * 100.0 * numCores;
    return cpuPercent;
}

int main() {
    // Test on a known PID - replace this with a real PID from your last run
    DWORD testPid;
    cout << "Enter a PID to check (from your process_list.exe output): ";
    cin >> testPid;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, testPid);

    if (hProcess == NULL) {
        cerr << "Failed to open process. Error code: " << GetLastError() << "\n";
        return 1;
    }

    cout << "Measuring CPU usage over 1 second...\n";
    double cpu = GetCpuUsagePercent(hProcess, 1000);
    double mem = GetMemoryUsageMB(hProcess);

    cout << fixed << setprecision(2);
    cout << "CPU Usage: " << cpu << "%\n";
    cout << "Memory Usage: " << mem << " MB\n";

    CloseHandle(hProcess);
    return 0;
}