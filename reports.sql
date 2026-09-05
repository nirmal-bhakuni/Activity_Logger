-- 1. Session overview — duration, idle time, idle percentage
SELECT 
    session_id,
    start_time,
    end_time,
    TIMESTAMPDIFF(SECOND, start_time, end_time) AS duration_seconds,
    total_idle_seconds,
    ROUND((total_idle_seconds / NULLIF(TIMESTAMPDIFF(SECOND, start_time, end_time), 0)) * 100, 2) AS idle_percentage
FROM sessions
WHERE end_time IS NOT NULL
ORDER BY session_id;

-- 2. Most-used applications (by number of times they appeared active, across all sessions)
SELECT 
    process_name,
    COUNT(*) AS times_seen,
    ROUND(AVG(cpu_usage), 2) AS avg_cpu,
    ROUND(AVG(memory_usage_mb), 2) AS avg_memory_mb
FROM process_snapshots
GROUP BY process_name
ORDER BY times_seen DESC;

-- 3. Highest CPU-consuming process per session (uses a subquery — good to show in a DBMS project)
SELECT ps.session_id, ps.process_name, ps.cpu_usage, ps.log_time
FROM process_snapshots ps
INNER JOIN (
    SELECT session_id, MAX(cpu_usage) AS max_cpu
    FROM process_snapshots
    GROUP BY session_id
) top ON ps.session_id = top.session_id AND ps.cpu_usage = top.max_cpu
ORDER BY ps.session_id;

-- 4. Active vs idle snapshot count per session (join sessions + process_snapshots)
SELECT 
    s.session_id,
    COUNT(CASE WHEN ps.is_idle = 1 THEN 1 END) AS idle_snapshots,
    COUNT(CASE WHEN ps.is_idle = 0 THEN 1 END) AS active_snapshots,
    COUNT(*) AS total_snapshots
FROM sessions s
JOIN process_snapshots ps ON s.session_id = ps.session_id
GROUP BY s.session_id
ORDER BY s.session_id;

-- 5. Memory usage trend — average memory per process over time (useful if you want to show a chart later)
SELECT 
    process_name,
    log_time,
    memory_usage_mb
FROM process_snapshots
WHERE session_id = (SELECT MAX(session_id) FROM sessions WHERE end_time IS NOT NULL)
ORDER BY log_time;