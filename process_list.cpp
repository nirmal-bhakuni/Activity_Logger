#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

int main() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create snapshot\n";
        return 1;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &entry)) {
        std::cerr << "Process32First failed\n";
        CloseHandle(snapshot);
        return 1;
    }

    std::cout << "Running processes:\n";
    std::cout << "----------------------\n";

    do {
        std::wcout << entry.szExeFile << L"\n";
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return 0;
}