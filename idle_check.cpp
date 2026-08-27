#include <windows.h>
#include <iostream>

using namespace std;

// Returns idle time in seconds since last keyboard/mouse input
double GetIdleTimeSeconds() {
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);

    if (!GetLastInputInfo(&lii)) {
        return -1.0;
    }

    DWORD currentTick = GetTickCount();
    DWORD idleTick = currentTick - lii.dwTime;

    return idleTick / 1000.0; // convert ms to seconds
}

int main() {
    cout << "Checking idle time... (move your mouse or type to reset it)\n";
    cout << "Press Ctrl+C to stop.\n\n";

    while (true) {
        double idleSeconds = GetIdleTimeSeconds();

        if (idleSeconds < 0) {
            cerr << "Failed to get idle time\n";
            break;
        }

        cout << "Idle for: " << idleSeconds << " seconds";
        
        // Example threshold: consider "idle" if no input for 60+ seconds
        if (idleSeconds >= 60) {
            cout << "  [STATUS: IDLE]";
        } else {
            cout << "  [STATUS: ACTIVE]";
        }
        
        cout << "\n";
        Sleep(2000); // check every 2 seconds
    }

    return 0;
}