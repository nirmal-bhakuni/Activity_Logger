#include <windows.h>
#include "idle_check.h"

// Returns idle time in seconds since last keyboard/mouse input
double GetIdleTimeSeconds()
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);

    if (!GetLastInputInfo(&lii))
    {
        return -1.0;
    }

    DWORD currentTick = GetTickCount();
    DWORD idleTick = currentTick - lii.dwTime;

    return idleTick / 1000.0;
}