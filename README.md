# Employee Activity Monitoring System

A Windows project for **Operating Systems + DBMS**. A small agent runs on an employee's PC, records which apps are open, how much CPU and memory they use, and whether the user is idle. It saves everything to a central MySQL database. A productivity score is then calculated from that data.

## What it does

- Lists the apps that have a visible window, using Windows system calls
- Measures CPU and memory for each app
- Detects idle time (no keyboard or mouse input)
- Saves the data to MySQL using three threads that share a queue
- Supports many PCs writing to one central database (each PC has its own employee ID)
- Calculates a productivity score per session and per employee, with a penalty for games and idle time
- Starts automatically when Windows starts (Startup folder shortcut)
- Prints a text report from the saved data

## OS concepts used

**Written in this project's own code**

- **Multithreading:** three threads (producer, consumer, reporter) made with `std::thread`
- **Producer-consumer with a bounded queue:** the producer scans apps and pushes into a queue (max 50). The consumer takes items out and writes them to MySQL. A mutex and a condition variable keep the two threads in step. The consumer sleeps when the queue is empty, and the producer waits when it is full.
- **Mutual exclusion:** one mutex guards the queue and a second mutex guards the session statistics
- **Deadlock prevention by lock ordering:** if a thread needs both locks, it always takes the queue lock first and the stats lock second. The consumer never holds both at once. Only the reporter thread takes both, in the fixed order.
- **Atomic variables:** `std::atomic` for the idle counter and the stop flag
- **Safe shutdown:** the Ctrl+C / window-close handler only sets a stop flag. The other threads stop, and `main()` closes the session. Only one thread ever uses the database connection at a time.

**Used from Windows (calls to the OS, not our own algorithms)**

- Process list (`CreateToolhelp32Snapshot`), window check (`EnumWindows`)
- CPU time (`GetProcessTimes`, `GetSystemTimes`) and memory (`GetProcessMemoryInfo`)
- Idle time (`GetLastInputInfo`)

## DBMS concepts used

- **Client-server database:** MySQL runs as a server. Agents on other PCs connect to it over the network.
- **Normalized schema:** `sessions` has many `process_snapshots`, linked by a foreign key
- **Data-driven settings:** the game list (`game_processes`) and score weights (`score_config`) are tables, so they change with one SQL statement and no recompiling
- **Views:** `session_scores` and `employee_scores` save the score calculation, so it is never rewritten
- **Queries:** joins, subqueries, `GROUP BY`, aggregates, and `CASE` inside `COUNT`

## Requirements

- Windows 10 or 11
- MinGW-w64 with `g++`, `gendef` and `dlltool`
- `make`
- MySQL Server 8.0

## Setup

### 1. Get the code

```
git clone <your-repo-url>
cd Activity_Logger
```

### 2. Create the database

Run this from the project folder. The SQL files are in the `database` folder.

```
mysql -u root -p -e "source database/setup_all.sql"
```

This runs, in order:

- `01_schema.sql` creates the tables
- `02_seed_data.sql` adds the starting game list and score weights
- `03_views.sql` creates the score views

It is safe to run again. It only creates what is missing.

Use this command, not `mysql ... < file.sql`. The `<` form does not work in PowerShell.

### 3. Create `config.ini`

Copy the example and edit it:

```
copy config.ini.example config.ini
```

Set the database host, user, password, and this PC's employee ID. `config.ini` holds a password and is in `.gitignore`. Do not commit it.

If you are running on the same PC as MySQL, use `host=localhost`. You can use `root` as the user, or create `monitor_user` with `database/04_remote_user.sql` (see step 5).

### 4. MySQL library files

The repo already includes `libmysql.dll`, `libmysql.a`, `libmysql.def` and the `include/` folder. To rebuild them on another PC:

1. Copy `libmysql.dll` from `C:\Program Files\MySQL\MySQL Server 8.0\lib\` into the project folder.
2. Make the MinGW import library:
   ```
   gendef libmysql.dll
   dlltool -d libmysql.def -l libmysql.a -D libmysql.dll
   ```
3. Copy the `include` folder from `C:\Program Files\MySQL\MySQL Server 8.0\` into the project folder.

Why: MySQL's own Connector/C++ is built for the MSVC compiler and does not link with MinGW. The C library (`libmysql`) with a MinGW-made `.a` file does.

### 5. Create the database user (and optionally let other PCs connect)

Edit the password in `database/04_remote_user.sql`, then run it on the PC that runs MySQL:

```
mysql -u root -p -e "source database/04_remote_user.sql"
```

This creates two accounts, because MySQL treats them as separate:

- `monitor_user`@`localhost` is for an agent on the same PC as MySQL
- `monitor_user`@`%` is for agents on other PCs

If every agent runs on the MySQL PC, you are done with this step. Only follow the rest if agents run on different PCs. On the server PC:

1. Edit `my.ini` (usually `C:\ProgramData\MySQL\MySQL Server 8.0\`). Under `[mysqld]` add:
   ```
   bind-address=0.0.0.0
   ```
2. Restart MySQL in an admin PowerShell:
   ```
   Restart-Service -Name "MySQL80"
   ```
3. Open the firewall port in an admin PowerShell:
   ```
   New-NetFirewallRule -DisplayName "MySQL Remote Access" -Direction Inbound -Protocol TCP -LocalPort 3306 -Action Allow
   ```
4. On each agent PC, set `host=` in `config.ini` to the server's IP address (find it with `ipconfig`).

If the server PC changes network (for example a different Wi-Fi), its IP changes and `config.ini` on the agent PCs must be updated.

### 6. Make sure MySQL is running

```
Get-Service -Name "MySQL80"
```

If it is stopped, start it from an admin PowerShell with `Start-Service -Name "MySQL80"`.

### 7. Test the connection and schema

Build and run the connection test after creating `config.ini`:

```
make test_connection.exe
.\test_connection.exe
```

The test reads the database settings from `config.ini`, connects to the
`activity_monitor` database, and runs a read-only query against the `sessions`
table. A successful run prints the number of sessions currently in the
database. It does not insert test data. A non-zero exit code means that the
configuration, connection, or schema check failed.

## Build

```
make                     # builds activity_monitor.exe
make test_connection.exe # builds the connection and schema test
make report.exe          # builds the report program
make all_targets         # builds every program
make clean               # deletes all .exe files
```

Build with `make`. Do not use the VS Code Run button. It compiles without the flags this project needs (`-I.\include`, `-lmysql`, `-lpsapi`, `-pthread`) and fails with `mysql.h: No such file`.

The Makefile needs a real **tab** (not spaces) before each command line. If you see `missing separator`, that is the cause.

## Run

### Start monitoring

```
.\activity_monitor.exe
```

- Connects to MySQL and starts a new session
- Every 5 seconds, scans the apps and saves their CPU, memory and idle state
- Runs for 2 minutes, or until Ctrl+C. The session is closed properly either way, including when the console window is closed.

To keep a copy of the output:

```
.\activity_monitor.exe > run_log.txt
```

Settings at the top of `activity_monitor.cpp`:

```cpp
const int SNAPSHOT_INTERVAL_SECONDS = 5;
const int TOTAL_RUN_SECONDS = 120;
const int IDLE_THRESHOLD_SECONDS = 60;
const int CONSUMER_DEMO_DELAY_MS = 700; // demo only, set to 0 for normal use
```

`CONSUMER_DEMO_DELAY_MS` slows the consumer on purpose, so the log shows producer and consumer running at the same time. Set it to `0` for normal use. If you change `SNAPSHOT_INTERVAL_SECONDS`, also update `snapshot_interval_sec` in the `score_config` table.

### Start automatically when Windows starts

1. Press `Win + R`, type `shell:startup`, press Enter.
2. Create a shortcut to `activity_monitor.exe` in that folder.
3. Open the shortcut's Properties and set **Start in** to the project folder. This is needed so `config.ini` is found.
4. Optional: set **Run** to *Minimized*.

### View the report

```
.\report.exe
```

`report.cpp` has the database host, user and password as constants at the top. Edit them before you build it.

You can also run the SQL directly. See `database/reports.sql`.

## Productivity score

```
score = 100 - game_weight * (% of session spent gaming) - idle_weight * (% of session spent idle)
```

- The score is kept between 0 and 100
- The default weights are 1.0 for games and 0.5 for idle time
- Sessions shorter than 30 seconds are not scored
- The weights are a policy choice, not a scientific formula

View the scores:

```sql
SELECT * FROM session_scores ORDER BY session_id DESC;
SELECT * FROM employee_scores;
```

Change the settings with no recompiling:

```sql
INSERT INTO game_processes VALUES ('SomeGame.exe');
UPDATE score_config SET game_weight = 1.5 WHERE id = 1;
```

To see the exact `.exe` names your monitor recorded:

```sql
SELECT DISTINCT process_name FROM process_snapshots;
```

## Project structure

```
Activity_Logger/
├── activity_monitor.cpp     Main agent (3 threads, writes to MySQL)
├── report.cpp               Text report from the saved data
├── process_list.cpp         Test program: lists apps with a visible window
├── process_stats.cpp        Test program: CPU and memory for one process ID
├── idle_check.cpp           Test program: live idle timer
├── test_connection.cpp      Read-only database connection and schema test
├── config.ini.example       Copy to config.ini and edit
├── Makefile                 Build rules
├── database/
│   ├── setup_all.sql        Runs 01, 02 and 03 in order
│   ├── 01_schema.sql        Tables
│   ├── 02_seed_data.sql     Game list and score weights
│   ├── 03_views.sql         Score views
│   ├── 04_remote_user.sql   Database user (local and remote accounts)
│   └── reports.sql          Example queries
├── libmysql.dll/.a/.def     MySQL client library files
└── include/                 MySQL headers
```

## Troubleshooting

- **"Connection failed:" with no reason shown:** check that MySQL is running, and that the `host` in `config.ini` is correct. After changing Wi-Fi, the server's IP may have changed.
- **"config.ini is missing database settings":** copy `config.ini.example` to `config.ini` and set `host`, `user`, `password`, and `database`.
- **"Schema check failed":** run `mysql -u root -p -e "source database/setup_all.sql"` to create the current schema, then run `.\test_connection.exe` again.
- **"Access denied" for `monitor_user` on the same PC:** the `localhost` account may be missing. Run `database/04_remote_user.sql`. MySQL treats `@'localhost'` and `@'%'` as different accounts.
- **`mysql.h: No such file or directory`:** you used the Run button. Use `make`.
- **`missing separator` from make:** a command line in the Makefile starts with spaces. Replace them with one Tab.
- **Service will not start ("Cannot open service"):** use an admin PowerShell.
- **Old sessions with no end time:** these are from runs before the safe-shutdown fix. Check with `SELECT session_id FROM sessions WHERE end_time IS NULL;`

## Known limitations

- **Windows only.** It uses Windows system calls directly.
- **Windows calls are still used.** Process listing, CPU, memory and idle time come from the standard Windows API, not from custom kernel driver code.
- **The score is simple.**
  - A game that is open but not being played still counts as gaming.
  - Browser games cannot be detected.
  - If a game has several windowed processes, its time can be over-counted.
- **Idle time is under-counted.** It only starts counting after 60 seconds of no input.
- **Session length runs a few seconds over.** A 120-second run lasts about 130 seconds, because the loop timing is not corrected for the time each scan takes. Each snapshot is therefore a little longer than 5 seconds, and game time is slightly under-counted.
- **Deadlock is prevented by a rule, not shown happening.** No thread takes the two locks in the reverse order, so a deadlock never occurs. The code shows the lock-ordering rule, not a deadlock being triggered and fixed.
- **SQL is built by joining text.** This is not safe against SQL injection. Prepared statements would fix it.
- **Security is basic.** The database password is stored as plain text in `config.ini`. The remote user can connect from any computer. This is fine for a demo, not for real use.
- **There is no dashboard yet.** Results are seen through `report.exe` or SQL queries. A web dashboard for employees and bosses is planned.
