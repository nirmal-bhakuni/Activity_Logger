"""
build_summary.py - turns one employee's saved activity into a SHORT summary.

There is NO AI in this file. It only reads MySQL and does arithmetic, so you can check
the numbers yourself before any of them are sent to Gemini.

Two different ideas are kept apart on purpose:
  apps_open_most_often  = apps that had a window open (says little: Chrome is always open)
  top_focused_apps      = apps the employee was actually working in (needs is_focused data)

Setup (once):   pip install mysql-connector-python
Run:            python ai/build_summary.py              (lists the employees in the database)
                python ai/build_summary.py EMP001       (prints the summary for one employee)

It reads the database login from config.ini ([database] section), the same one the C++ agent uses.
"""
import configparser
import json
import sys

import mysql.connector

# The monitoring programs themselves. Their console window is "open" all day, which is not the employee's work.
OWN_PROGRAMS = {"activity_monitor.exe", "report.exe"}

# Background / system windows. They are open all day and say nothing about the work,
# so they are left out of the "top apps" list.
IGNORED_APPS = OWN_PROGRAMS | {
    "explorer.exe", "textinputhost.exe", "applicationframehost.exe", "systemsettings.exe",
    "shellhost.exe", "searchhost.exe", "startmenuexperiencehost.exe", "widgets.exe",
    "runtimebroker.exe", "taskmgr.exe", "dllhost.exe", "msedgewebview2.exe",
}
# Pop-up overlays that grab focus for a moment (search box, start menu, input panel).
FOCUS_IGNORED_APPS = OWN_PROGRAMS | {
    "textinputhost.exe", "searchhost.exe", "startmenuexperiencehost.exe", "shellhost.exe",
    "runtimebroker.exe", "dllhost.exe", "widgets.exe", "taskmgr.exe",
}
TOP_APPS = 8            # how many apps to include
RECENT_SESSIONS = 10    # how many recent sessions to include


def connect():
    cfg = configparser.ConfigParser(interpolation=None)   # interpolation=None: passwords with % are safe
    if not cfg.read("config.ini"):
        sys.exit("config.ini not found. Run this from the project folder.")
    db = cfg["database"]
    return mysql.connector.connect(
        host=db.get("host", "localhost"),
        user=db.get("user"),
        password=db.get("password", ""),
        database=db.get("database", "activity_monitor"),
        port=int(db.get("port", "3306")),
    )


def list_employees(conn):
    cur = conn.cursor()
    cur.execute("SELECT employee_id, sessions_counted, avg_score FROM employee_scores ORDER BY employee_id")
    rows = cur.fetchall()
    cur.close()
    return rows


def data_limits(focus_sessions):
    limits = [
        "Idle time only counts after 60 seconds without keyboard or mouse input.",
        "Games are detected only by .exe name from a fixed list; browser games are invisible.",
        "'apps_open_most_often' only means a window existed; it does not mean the app was used.",
    ]
    if focus_sessions:
        limits += [
            "Focus is a sample taken once every 5 seconds, so short switches between apps are missed.",
            "Only the app's process name is known, never window titles or content. "
            "A browser focus does not say whether the page was work or not.",
            "Windows Store apps can appear as ApplicationFrameHost.exe instead of their own name.",
            "Only %d session(s) have focus data. Older sessions were recorded before focus tracking existed." % focus_sessions,
        ]
    else:
        limits.append("No focus data yet: the monitor does not know which app the employee was actually using.")
    return limits


def build_summary(conn, employee_id):
    cur = conn.cursor(dictionary=True)

    cur.execute("SELECT snapshot_interval_sec FROM score_config WHERE id = 1")
    row = cur.fetchone()
    interval = int(row["snapshot_interval_sec"]) if row else 5

    # Totals over all scored sessions (session_scores already skips sessions that are too short)
    cur.execute(
        "SELECT COUNT(*) AS n, SUM(session_sec) AS total_sec, SUM(game_sec) AS game_sec, "
        "       SUM(idle_sec) AS idle_sec, AVG(productivity_score) AS avg_score "
        "FROM session_scores WHERE employee_id = %s", (employee_id,))
    t = cur.fetchone()
    if not t or not t["n"]:
        cur.close()
        return None
    total_sec = float(t["total_sec"])

    # Most recent sessions, then put them back in time order (oldest first)
    cur.execute(
        "SELECT session_id, start_time, session_sec, game_sec, idle_sec, productivity_score "
        "FROM session_scores WHERE employee_id = %s ORDER BY session_id DESC LIMIT %s",
        (employee_id, RECENT_SESSIONS))
    recent = list(reversed(cur.fetchall()))
    recent_out = [{
        "session_id": r["session_id"],
        "date": str(r["start_time"])[:10],
        "minutes": round(float(r["session_sec"]) / 60, 1),
        "score": float(r["productivity_score"]),
        "game_percent": round(100 * float(r["game_sec"]) / float(r["session_sec"]), 1),
        "idle_percent": round(100 * float(r["idle_sec"]) / float(r["session_sec"]), 1),
    } for r in recent]

    # Apps: how many snapshot rows each app has in this employee's scored sessions
    cur.execute(
        "SELECT ps.process_name AS app, COUNT(*) AS rows_seen, MAX(gp.process_name IS NOT NULL) AS is_game "
        "FROM process_snapshots ps "
        "JOIN session_scores ss ON ss.session_id = ps.session_id "
        "LEFT JOIN game_processes gp ON gp.process_name = ps.process_name "
        "WHERE ss.employee_id = %s GROUP BY ps.process_name", (employee_id,))
    apps = []
    for r in cur.fetchall():
        if r["app"].lower() in IGNORED_APPS:
            continue
        seconds = int(r["rows_seen"]) * interval
        apps.append({
            "app": r["app"],
            "approx_minutes": round(seconds / 60, 1),
            "share_percent": round(min(100.0, 100 * seconds / total_sec), 1),   # capped: see notes
            "is_game": bool(r["is_game"]),
        })
    apps.sort(key=lambda a: a["approx_minutes"], reverse=True)
    cur.close()

    # Focus: which app had the keyboard focus. Only rows where is_focused is NOT NULL were recorded
    # after focus tracking existed. Idle rows are skipped (a game left in front of a sleeping PC is not "playing").
    cur = conn.cursor(dictionary=True)
    cur.execute(
        "SELECT COUNT(DISTINCT ps.session_id) AS n FROM process_snapshots ps "
        "JOIN session_scores ss ON ss.session_id = ps.session_id "
        "WHERE ss.employee_id = %s AND ps.is_focused IS NOT NULL", (employee_id,))
    focus_sessions = int(cur.fetchone()["n"])

    focus_apps = []
    focus_total_sec = 0
    if focus_sessions:
        cur.execute(
            "SELECT ps.process_name AS app, COUNT(*) AS rows_focused, MAX(gp.process_name IS NOT NULL) AS is_game "
            "FROM process_snapshots ps "
            "JOIN session_scores ss ON ss.session_id = ps.session_id "
            "LEFT JOIN game_processes gp ON gp.process_name = ps.process_name "
            "WHERE ss.employee_id = %s AND ps.is_focused = 1 AND ps.is_idle = 0 "
            "GROUP BY ps.process_name", (employee_id,))
        rows = [r for r in cur.fetchall() if r["app"].lower() not in FOCUS_IGNORED_APPS]
        focus_total_sec = sum(int(r["rows_focused"]) for r in rows) * interval
        for r in rows:
            sec = int(r["rows_focused"]) * interval
            focus_apps.append({
                "app": r["app"],
                "focused_minutes": round(sec / 60, 1),
                "share_of_focus_percent": round(100 * sec / focus_total_sec, 1) if focus_total_sec else 0.0,
                "is_game": bool(r["is_game"]),
            })
        focus_apps.sort(key=lambda a: a["focused_minutes"], reverse=True)
    cur.close()
    game_focus_percent = round(float(sum(a["share_of_focus_percent"] for a in focus_apps if a["is_game"])), 1) if focus_apps else None

    # Simple trend: average score of the newer half minus the older half (needs 4+ sessions)
    trend = None
    scores = [s["score"] for s in recent_out]
    if len(scores) >= 4:
        half = len(scores) // 2
        trend = round(sum(scores[-half:]) / half - sum(scores[:half]) / half, 1)

    return {
        "employee_id": employee_id,
        "sessions_scored": int(t["n"]),
        "total_monitored_minutes": round(total_sec / 60, 1),
        "average_score": round(float(t["avg_score"]), 1),
        "game_percent": round(100 * float(t["game_sec"]) / total_sec, 1),
        "idle_percent": round(100 * float(t["idle_sec"]) / total_sec, 1),
        "score_trend_newer_minus_older": trend,
        "focus_tracked_sessions": focus_sessions,
        "focused_minutes_total": round(focus_total_sec / 60, 1),
        "game_focus_percent": game_focus_percent,
        "top_focused_apps": focus_apps[:TOP_APPS],
        "apps_open_most_often": apps[:TOP_APPS],
        "recent_sessions": recent_out,
        "data_limits": data_limits(focus_sessions),
    }


def main():
    conn = connect()
    try:
        if len(sys.argv) < 2:
            rows = list_employees(conn)
            if not rows:
                print("No scored sessions in the database yet. Run activity_monitor.exe first.")
                return
            print("Employees with scored sessions:")
            for emp, n, avg in rows:
                print("  %-10s sessions: %-3s average score: %s" % (emp, n, avg))
            print("\nRun again with an employee id, for example:  python ai/build_summary.py %s" % rows[0][0])
            return
        summary = build_summary(conn, sys.argv[1])
        if summary is None:
            sys.exit("No scored sessions for employee '%s'." % sys.argv[1])
        print(json.dumps(summary, indent=2))
    finally:
        conn.close()


if __name__ == "__main__":
    main()