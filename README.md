# Activity Logger

Windows activity-monitoring project for an Operating Systems and DBMS
demonstration. The native monitor records visible applications, CPU, memory,
foreground-window state, and idle time. It writes the results to MySQL and
calculates productivity scores. An optional Flask dashboard and optional
Gemini integration provide browser-based reporting and AI-generated summaries.

## What is included

- `activity_monitor.exe`: records activity for a two-minute session.
- `test_connection.exe`: verifies the database connection and schema without
  inserting test data.
- `report.exe`: prints SQL reports. Its database credentials are currently
  constants in `report.cpp`, so edit that file before using it.
- `web/`: optional Flask employee and boss dashboard.
- `ai/`: optional summary and Gemini insight tools.
- `database/`: schema, seed data, views, remote-user SQL, and legacy migrations.

## Prerequisites

Install these on a new Windows 10 or Windows 11 device:

1. **Git**
2. **MySQL Server 8.0**, including the MySQL command-line client
3. **MinGW-w64**, with `g++`, `gendef`, and `dlltool` on `PATH`
4. **GNU Make** (`make` or `mingw32-make`)
5. **Python 3.9 or newer** (only needed for the dashboard or AI tools)

Confirm the tools from PowerShell:

```powershell
git --version
mysql --version
g++ --version
make --version
py --version
```

If `mysql`, `g++`, or `make` is not recognized, add the installation
directories to the system or user `PATH`, then open a new PowerShell window.

## 1. Get the project

```powershell
git clone <repository-url>
Set-Location .\Activity_Logger
```

Always run the commands below from the project root. The programs look for
`config.ini` in the current directory.

## 2. Create the MySQL database

Make sure the MySQL service is running:

```powershell
Get-Service -Name MySQL80
# Run in an Administrator PowerShell only if it is stopped:
Start-Service -Name MySQL80
```

Create the database, tables, seed data, and score views:

```powershell
mysql -u root -p -e "source database/setup_all.sql"
```

Enter the MySQL root password when prompted. `setup_all.sql` runs these files
in order:

1. `database/01_schema.sql` - creates the `activity_monitor` database and
   current tables
2. `database/02_seed_data.sql` - inserts the initial game list and score
   settings
3. `database/03_views.sql` - creates `session_scores` and `employee_scores`

The scripts use `IF NOT EXISTS` or `CREATE OR REPLACE`; running them again does
not delete existing rows.

### Optional: create an application database user

For a local-only installation, using a local MySQL account is sufficient. For
a safer setup, edit the password in `database/04_remote_user.sql`, then run:

```powershell
mysql -u root -p -e "source database/04_remote_user.sql"
```

This creates `monitor_user` accounts for both `localhost` and remote clients.
The SQL file contains the exact privileges granted to the user.

For agents on another PC, also do the following on the MySQL server:

1. In `my.ini` (normally
   `C:\ProgramData\MySQL\MySQL Server 8.0\my.ini`), add under `[mysqld]`:

   ```ini
   bind-address=0.0.0.0
   ```

2. Restart MySQL from an Administrator PowerShell:

   ```powershell
   Restart-Service -Name MySQL80
   ```

3. Allow TCP port 3306:

   ```powershell
   New-NetFirewallRule -DisplayName "MySQL Remote Access" `
     -Direction Inbound -Protocol TCP -LocalPort 3306 -Action Allow
   ```

4. Find the server address with `ipconfig` and use that address as `host` in
   each agent's `config.ini`.

Do not expose MySQL to the public internet. Restrict the firewall rule to the
private network when possible.

## 3. Configure the monitor

Create the local configuration file:

```powershell
Copy-Item .\config.ini.example .\config.ini
notepad .\config.ini
```

Set at least these values:

```ini
[database]
host=localhost
user=monitor_user
password=YOUR_DATABASE_PASSWORD
database=activity_monitor

[employee]
employee_id=EMP001
employee_name=Your Name
```

Use the MySQL server's IP address instead of `localhost` on a different
machine. Give every monitored PC its own `employee_id`. The optional Gemini
section can remain unchanged until AI features are configured.

`config.ini` contains secrets and is ignored by Git. Never commit it or share
its contents.

## 4. Rebuild the MySQL client files if needed

The repository includes `libmysql.dll`, `libmysql.a`, `libmysql.def`, and the
MySQL headers under `include/`. If these files are missing or incompatible
with the new device's MinGW installation, install MySQL Server 8.0 and copy:

- `libmysql.dll` from the MySQL `lib` directory to the project root
- the MySQL `include` directory to the project root as `include`

From the project root, recreate the MinGW import library:

```powershell
gendef .\libmysql.dll
dlltool -d .\libmysql.def -l .\libmysql.a -D libmysql.dll
```

The DLL must be beside the executables when they run. The Makefile links the
native programs with the MySQL C client, Windows PSAPI, and pthread support.

## 5. Build and verify

```powershell
make all_targets
.\test_connection.exe
```

Expected output includes `Connected to activity_monitor DB successfully!` and
`Schema check passed`.

Available build targets:

```powershell
make                     # activity_monitor.exe
make test_connection.exe
make report.exe
make all_targets
make clean
```

Use `make`, not the VS Code Run button. The project requires the include,
library, PSAPI, C++17, and pthread flags defined in the Makefile. If Make
reports `missing separator`, the Makefile was edited with spaces instead of
the required command tabs.

## 6. Run the native monitor

```powershell
.\activity_monitor.exe
```

The monitor:

- opens a new database session;
- scans visible applications every five seconds;
- records CPU, memory, idle, and focus information;
- runs for about two minutes; and
- closes the session on normal exit or Ctrl+C.

Save console output if required:

```powershell
.\activity_monitor.exe | Tee-Object .\run_log.txt
```

The demo settings are in `activity_monitor.cpp`:

```cpp
SNAPSHOT_INTERVAL_SECONDS = 5;
TOTAL_RUN_SECONDS = 120;
IDLE_THRESHOLD_SECONDS = 60;
CONSUMER_DEMO_DELAY_MS = 700;
```

Set `CONSUMER_DEMO_DELAY_MS` to `0` for normal operation. If the snapshot
interval is changed, update `snapshot_interval_sec` in `score_config` too.

## 7. View scores and reports

Run the text report:

```powershell
.\report.exe
```

Before building `report.exe`, edit `DB_HOST`, `DB_USER`, `DB_PASS`, and
`DB_NAME` near the top of `report.cpp`. Unlike the monitor and web application,
the report program does not currently read `config.ini`.

The saved score views can also be queried directly:

```powershell
mysql -u monitor_user -p activity_monitor `
  -e "SELECT * FROM session_scores ORDER BY session_id DESC; SELECT * FROM employee_scores;"
```

The score is calculated as:

```text
100 - game_weight * game_percent - idle_weight * idle_percent
```

Scores are clamped to 0-100. Sessions shorter than `min_session_sec` are
excluded. The default configuration uses a game weight of `1.0`, an idle
weight of `0.5`, and a five-second snapshot interval.

## Optional web dashboard

Create a Python virtual environment and install the dashboard dependencies:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-web.txt
```

Fresh databases already contain the current `employees` and `ai_insights`
tables. For a database created by an older version, run the applicable
migrations once, in this order:

```powershell
mysql -u root -p -e "source database/migrations/001_add_is_focused.sql"
mysql -u root -p -e "source database/migrations/002_create_ai_insights.sql"
mysql -u root -p -e "source database/migrations/003_add_employees.sql"
```

Create dashboard accounts. Passwords are prompted privately and must contain
at least 12 characters:

```powershell
.\.venv\Scripts\python.exe web/bootstrap_account.py `
  --employee-id EMP001 --name "Employee One" --role employee

.\.venv\Scripts\python.exe web/bootstrap_account.py `
  --employee-id BOSS001 --name "Team Lead" --role boss
```

Start the dashboard from the project root:

```powershell
$env:WEB_SECRET_KEY = [guid]::NewGuid().ToString("N")
.\.venv\Scripts\python.exe -m web.app
```

Open <http://127.0.0.1:5000/>. Use a durable random `WEB_SECRET_KEY` outside
development. Set `$env:WEB_COOKIE_SECURE = "true"` only when the app is behind
HTTPS. Flask's development server is for local use, not public deployment.

If the configuration file is elsewhere, set its absolute path:

```powershell
$env:ACTIVITY_CONFIG = "C:\Path\To\config.ini"
```

## Optional Gemini insights

The AI tools use the Gemini API only when explicitly run. They send a
sanitized summary, not the employee ID or machine name. App names are cleaned
before the request, but they are still activity data; follow your
organization's privacy policy.

Add a key and a model to `config.ini`:

```ini
[gemini]
api_key=YOUR_GEMINI_API_KEY
model=MODEL_NAME
```

Or keep the key out of the file for the current PowerShell session:

```powershell
$env:GEMINI_API_KEY = "YOUR_GEMINI_API_KEY"
```

Check available models and test the key:

```powershell
.\.venv\Scripts\python.exe ai/gemini_test.py
```

Build a numeric summary:

```powershell
.\.venv\Scripts\python.exe ai/build_summary.py
.\.venv\Scripts\python.exe ai/build_summary.py EMP001
```

Generate and save an insight:

```powershell
.\.venv\Scripts\python.exe ai/insights.py EMP001
```

Useful safe alternatives:

```powershell
.\.venv\Scripts\python.exe ai/insights.py EMP001 --show-prompt
.\.venv\Scripts\python.exe ai/insights.py EMP001 --latest
.\.venv\Scripts\python.exe ai/insights.py EMP001 --no-save
```

Insights require scored sessions and normally require focus-tracking data.
`--force` can be used for older data, but the result will be weaker.

## Automatic startup

To start the monitor when Windows logs in:

1. Press `Win+R`, enter `shell:startup`, and press Enter.
2. Create a shortcut to `activity_monitor.exe`.
3. In the shortcut properties, set **Start in** to the project root.
4. Optionally set **Run** to **Minimized**.

The **Start in** directory is required so the monitor can find `config.ini`.

## Troubleshooting

| Problem | Fix |
|---|---|
| `config.ini is missing` | Copy `config.ini.example` to `config.ini`, edit the database values, and run from the project root. |
| `Connection failed` | Check `Get-Service MySQL80`, host, port, username, password, firewall, and server IP. |
| `Schema check failed` | Run `mysql -u root -p -e "source database/setup_all.sql"` and retry the connection test. |
| `Access denied` for `monitor_user` | Run `database/04_remote_user.sql`; MySQL treats `user@localhost` and `user@%` as different accounts. |
| `mysql.h: No such file` | Check the project `include` directory and build with `make`, not the VS Code Run button. |
| `libmysql.dll was not found` | Put the DLL beside the `.exe` files or add its directory to `PATH`. |
| `make` is not recognized | Install GNU Make or use the installed `mingw32-make` command. |
| Dashboard cannot connect | Run it from the project root, confirm `.venv` dependencies, and check `[database]` in `config.ini`. |
| No scores appear | A completed session must be at least `min_session_sec` seconds long. |

## Known limitations

- Windows-only implementation using Windows process, memory, CPU, window, and
  idle-time APIs.
- A game is counted when its process is visible; browser games are not
  detected and several game processes may over-count time.
- Idle time begins only after 60 seconds without keyboard or mouse input.
- Focus is sampled every five seconds, so very short application switches can
  be missed.
- The native monitor stores database credentials in `config.ini`, and the
  current remote-user example allows `%`; use a restricted network and
  production-grade secret management for real deployments.
- Do not expose Flask's development server directly to the internet.

## Project structure

```text
Activity_Logger/
├── activity_monitor.cpp       Native monitoring agent
├── report.cpp                 Text report program
├── test_connection.cpp        Read-only connection/schema test
├── Makefile                   MinGW build targets
├── config.ini.example         Configuration template
├── database/
│   ├── setup_all.sql          Fresh-install entry point
│   ├── 01_schema.sql          Tables and database
│   ├── 02_seed_data.sql       Game list and score settings
│   ├── 03_views.sql           Productivity score views
│   ├── 04_remote_user.sql     Optional application accounts
│   ├── migrations/             Upgrades for older databases
│   └── reports.sql             Example SQL reports
├── web/                       Flask dashboard and account helper
├── ai/                        Summary and Gemini integration
├── include/                   MySQL headers
└── libmysql.dll/.a/.def       MySQL client runtime/import files
```
