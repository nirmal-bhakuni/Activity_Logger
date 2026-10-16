#include <windows.h>
#include <psapi.h>
#include "process_stats.h"

ULONGLONG FileTimeToInt(const FILETIME &ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

double GetMemoryUsageMB(HANDLE hProcess)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024.0 * 1024.0);

    return -1.0;
}

double GetCpuUsagePercent(HANDLE hProcess, DWORD intervalMs)
{
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
    ULONGLONG sysTime1 = FileTimeToInt(sysKernel1) + FileTimeToInt(sysUser1);
    ULONGLONG sysTime2 = FileTimeToInt(sysKernel2) + FileTimeToInt(sysUser2);
    ULONGLONG procDelta = procTime2 - procTime1;
    ULONGLONG sysDelta = sysTime2 - sysTime1;

    if (sysDelta == 0)
        return 0.0;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    return (double(procDelta) / double(sysDelta)) * 100.0 * numCores;
}
