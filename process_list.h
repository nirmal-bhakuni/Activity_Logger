#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ProcessInfo
{
    DWORD pid;
    std::string name;
};

bool ProcessHasVisibleWindow(DWORD pid);
std::string WideToNarrow(const wchar_t *wstr);
std::vector<ProcessInfo> GetUserFacingProcesses();
