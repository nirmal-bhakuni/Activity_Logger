#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>

using namespace std;

// Struct to hold PID -> whether it has a visible window
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
        return FALSE; // stop enumerating, found a match
    }

    return TRUE; // keep looking
}

bool ProcessHasVisibleWindow(DWORD pid) {
    WindowCheckData data;
    data.pid = pid;
    data.hasWindow = false;

    EnumWindows(EnumWindowsCallback, (LPARAM)&data);

    return data.hasWindow;
}

int main() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        cerr << "Failed to create snapshot\n";
        return 1;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &entry)) {
        cerr << "Process32First failed\n";
        CloseHandle(snapshot);
        return 1;
    }

    cout << "User-facing processes (with visible windows):\n";
    cout << "----------------------------------------------\n";

    do {
        DWORD pid = entry.th32ProcessID;

        if (ProcessHasVisibleWindow(pid)) {
            wcout << L"PID: " << pid << L"  |  " << entry.szExeFile << L"\n";
        }

    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return 0;
}