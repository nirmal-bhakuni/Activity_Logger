#pragma once

#include <windows.h>

ULONGLONG FileTimeToInt(const FILETIME &ft);
double GetMemoryUsageMB(HANDLE hProcess);
double GetCpuUsagePercent(HANDLE hProcess, DWORD intervalMs = 200);
