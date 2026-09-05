# Employee Activity Monitoring System

A Windows-based system activity monitor built for an Operating Systems + DBMS project. It tracks running applications, their CPU/memory usage, and system idle time, logging everything to a MySQL database with a normalized relational schema.

## What It Does

- Enumerates user-facing processes (apps with a visible window) using the Windows Toolhelp32 API
- Measures per-process CPU usage and memory consumption using WinAPI system calls
- Detects system-wide idle time (no keyboard/mouse input) using `GetLastInputInfo`
- Logs all data into a MySQL database, organized into **sessions** (one per monitoring run) and **process_snapshots** (one row per process per check interval)
- Generates a formatted report from the logged data using SQL joins and aggregation

## Why This Demonstrates OS + DBMS Concepts

**Operating Systems:**
- Direct use of Windows system calls (`CreateToolhelp32Snapshot`, `GetProcessTimes`, `GetSystemTimes`, `GetProcessMemoryInfo`, `GetLastInputInfo`) rather than third-party abstraction libraries
- Manual CPU usage calculation from raw process/system time deltas (not a built-in percentage)
- Signal handling for graceful process termination (`SetConsoleCtrlHandler`)

**DBMS:**
- Normalized two-table schema (`sessions` 1-to-many `process_snapshots`) with a foreign key relationship, rather than one flat table
- Aggregate queries (`AVG`, `COUNT`, `GROUP BY`)
- Subqueries and joins (finding max CPU usage per session)
- `CASE` expressions for conditional aggregation (active vs. idle snapshot counts)

## Prerequisites

- Windows 10/11
- MinGW-w64 (with `g++`, `gendef`, and `dlltool` — these come with the standard MinGW-w64 toolchain)
- MySQL Server 8.0 (Community Edition is fine)
- `make` (usually bundled with MinGW-w64; check with `make --version`)

## Setup Instructions

### 1. Clone the repository

```
git clone <your-repo-url>
cd Activity_Logger
```

### 2. Set up the database

Log into MySQL and run the schema file:

```
mysql -u root -p < schema.sql
```

This creates the `activity_monitor` database with the `sessions` and `process_snapshots` tables.

### 3. Set your MySQL password in the source files

Open `activity_monitor.cpp` and `report.cpp`, and replace:

```cpp
const char* DB_PASS = "YOUR_PASSWORD";
```

with your actual MySQL root password.

### 4. MySQL Connector setup (if `libmysql.a` is not already present)

This repo already includes `libmysql.dll`, `libmysql.lib`, `libmysql.a`, `libmysql.def`, and the `include/` folder needed to compile against MySQL's C API. If you need to regenerate these on a different machine:

1. Locate `libmysql.dll` in your MySQL Server install, typically at:
   `C:\Program Files\MySQL\MySQL Server 8.0\lib\libmysql.dll`
2. Copy it into the project folder
3. Generate a MinGW-compatible import library:
   ```
   gendef libmysql.dll
   dlltool -d libmysql.def -l libmysql.a -D libmysql.dll
   ```
4. Copy the `include` folder from the same MySQL Server install location into a local `include/` folder in this project

This step is necessary because MySQL's official Connector/C++ binaries are built for MSVC and are not directly linkable with MinGW's `g++`. Using the C API (`libmysql`) with a MinGW-generated `.a` import library avoids that incompatibility.

### 5. Make sure MySQL Server is running

```
Get-Service -Name "MySQL80"
```

If it's stopped, start it (requires an elevated/admin PowerShell):

```
Start-Service -Name "MySQL80"
```

## Building

This project uses a Makefile. From the project folder:

```
make                # builds activity_monitor.exe (default)
make all_targets     # builds all executables (monitor, report, and standalone test tools)
make report.exe      # builds only the reporting tool
make clean           # removes all compiled .exe files
```

## Running

### Start monitoring

```
.\activity_monitor.exe
```

This will:
- Connect to MySQL and create a new session record
- Every 5 seconds, scan running user-facing processes, record CPU/memory usage, and check idle status
- Run for 2 minutes by default, or until interrupted with Ctrl+C (the session will still be closed out properly on interrupt)

Configuration (interval, run duration, idle threshold) can be adjusted at the top of `activity_monitor.cpp`:

```cpp
const int SNAPSHOT_INTERVAL_SECONDS = 5;
const int TOTAL_RUN_SECONDS = 120;
const int IDLE_THRESHOLD_SECONDS = 60;
```

### View the report

```
.\report.exe
```

This prints:
- A session overview (duration, idle time, idle percentage)
- Most-used applications (by frequency, average CPU, average memory)
- The highest CPU-consuming process per session
- Active vs. idle snapshot counts per session

## Project Structure

```
Activity_Logger/
├── activity_monitor.cpp    # Main monitoring program
├── report.cpp               # Reporting/query program
├── process_list.cpp         # Standalone: lists user-facing processes
├── process_stats.cpp        # Standalone: CPU/memory for a single PID
├── idle_check.cpp           # Standalone: live idle time monitor
├── test_connection.cpp      # Standalone: MySQL connection test
├── schema.sql                # Database schema
├── Makefile                  # Build rules for all programs
├── libmysql.dll/.lib/.a/.def # MySQL C API client library files
└── include/                   # MySQL C API headers
```

## Known Limitations

- **Windows-only.** Uses WinAPI directly (Toolhelp32, GetLastInputInfo, etc.), so it will not run on Linux/macOS without a significant rewrite.
- **Graceful shutdown only handles Ctrl+C / window close**, not hard crashes (e.g., access violations) or forceful termination (`taskkill /F`). A session interrupted that way will be left with a `NULL` end_time.
- **Idle detection is system-wide, not per-application.** It reflects whether the user is providing any keyboard/mouse input at all, not whether a specific tracked process is "idle."
- **CPU usage is sampled over a short window (200ms) per process**, which is efficient for scanning multiple processes quickly but is a snapshot estimate rather than a continuously averaged value.
- **Single-machine monitoring.** This does not yet support multiple client machines reporting to one central server; the "client-server" aspect refers to the MySQL server architecture, not multi-agent deployment.