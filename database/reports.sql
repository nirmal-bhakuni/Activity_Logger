-- ============================================================
-- reports.sql  -  Example queries for viewing the collected data
-- ============================================================
-- Read-only: these queries do not change any data.
-- report.exe runs the first four of these from C++.
-- ============================================================

USE activity_monitor;

-- 1. Session overview: length, idle time and idle percentage
SELECT
    session_id, employee_id, machine_name, start_time, end_time,
    TIMESTAMPDIFF(SECOND, start_time, end_time) AS duration_seconds,
    total_idle_seconds,
    ROUND((total_idle_seconds / NULLIF(TIMESTAMPDIFF(SECOND, start_time, end_time), 0)) * 100, 2) AS idle_percentage
FROM sessions
WHERE end_time IS NOT NULL
ORDER BY session_id;

-- 2. Most-used applications across all sessions
SELECT
    process_name,
    COUNT(*) AS times_seen,
    ROUND(AVG(cpu_usage), 2) AS avg_cpu,
    ROUND(AVG(memory_usage_mb), 2) AS avg_memory_mb
FROM process_snapshots
GROUP BY process_name
ORDER BY times_seen DESC;

-- 3. Highest CPU reading in each session (subquery + join)
SELECT ps.session_id, ps.process_name, ps.cpu_usage, ps.log_time
FROM process_snapshots ps
INNER JOIN (
    SELECT session_id, MAX(cpu_usage) AS max_cpu
    FROM process_snapshots
    GROUP BY session_id
) top ON ps.session_id = top.session_id AND ps.cpu_usage = top.max_cpu
ORDER BY ps.session_id;

-- 4. Active vs idle snapshot counts per session (CASE inside COUNT)
SELECT
    s.session_id,
    COUNT(CASE WHEN ps.is_idle = 1 THEN 1 END) AS idle_snapshots,
    COUNT(CASE WHEN ps.is_idle = 0 THEN 1 END) AS active_snapshots,
    COUNT(*) AS total_snapshots
FROM sessions s
JOIN process_snapshots ps ON s.session_id = ps.session_id
GROUP BY s.session_id
ORDER BY s.session_id;

-- 5. Per-employee totals (shows the multi-employee side)
SELECT
    employee_id,
    machine_name,
    COUNT(*) AS sessions,
    SUM(total_idle_seconds) AS total_idle_seconds
FROM sessions
GROUP BY employee_id, machine_name
ORDER BY employee_id;

-- 6. Productivity scores (from the views in 03_views.sql)
SELECT * FROM session_scores ORDER BY session_id DESC;
SELECT * FROM employee_scores;

-- 7. Find sessions that were never closed (should be none for new runs)
SELECT session_id, start_time FROM sessions WHERE end_time IS NULL;
