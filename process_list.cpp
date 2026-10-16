#include <windows.h>
#include <tlhelp32.h>
#include "process_list.h"

struct WindowCheckData
{
    DWORD pid;
    bool hasWindow;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    WindowCheckData *data = (WindowCheckData *)lParam;

    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == data->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL)
    {
        data->hasWindow = true;
        return FALSE;
    }

    return TRUE;
}

bool ProcessHasVisibleWindow(DWORD pid)
{
    WindowCheckData data;
    data.pid = pid;
    data.hasWindow = false;

    EnumWindows(EnumWindowsCallback, (LPARAM)&data);

    return data.hasWindow;
}

std::string WideToNarrow(const wchar_t *wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size <= 0)
        return "";

    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, NULL, NULL);
    return result;
}

std::vector<ProcessInfo> GetUserFacingProcesses()
{
    std::vector<ProcessInfo> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (ProcessHasVisibleWindow(entry.th32ProcessID))
            {
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
