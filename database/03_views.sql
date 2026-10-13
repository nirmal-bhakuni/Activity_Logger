-- ============================================================
-- 03_views.sql  -  Saved queries that calculate productivity scores
-- ============================================================
-- WHAT THIS FILE DOES
--   Creates two views. A view is a saved query: it always shows current
--   data, so nothing needs recalculating when new sessions are added.
--   Safe to run more than once (CREATE OR REPLACE).
--
-- HOW TO USE
--   SELECT * FROM session_scores ORDER BY session_id DESC;
--   SELECT * FROM employee_scores;
--
-- HOW THE SCORE WORKS
--   game_sec = (number of snapshot rows for game processes) * snapshot_interval_sec
--   idle_sec = sessions.total_idle_seconds
--   score    = 100 - game_weight * 100 * game_sec / session_sec
--                  - idle_weight * 100 * idle_sec / session_sec
--   The score is kept between 0 and 100. Sessions shorter than
--   min_session_sec are skipped.
--
-- KNOWN LIMITS
--   - A game that is open but not being played still counts as gaming.
--   - Browser games cannot be detected.
--   - If a game has several windowed processes, its time can be over-counted.
-- ============================================================

USE activity_monitor;

-- Score for every finished session
CREATE OR REPLACE VIEW session_scores AS
SELECT
    b.session_id,
    b.employee_id,
    b.machine_name,
    b.start_time,
    b.session_sec,
    LEAST(b.game_sec, b.session_sec) AS game_sec,
    b.idle_sec,
    ROUND(GREATEST(0, 100
        - c.game_weight * 100 * LEAST(b.game_sec, b.session_sec) / b.session_sec
        - c.idle_weight * 100 * b.idle_sec / b.session_sec), 1) AS productivity_score
FROM (
    SELECT
        s.session_id,
        s.employee_id,
        s.machine_name,
        s.start_time,
        TIMESTAMPDIFF(SECOND, s.start_time, s.end_time) AS session_sec,
        COALESCE(g.game_rows, 0) * cfg.snapshot_interval_sec AS game_sec,
        s.total_idle_seconds AS idle_sec
    FROM sessions s
    CROSS JOIN score_config cfg
    LEFT JOIN (
        SELECT ps.session_id, COUNT(*) AS game_rows
        FROM process_snapshots ps
        JOIN game_processes gp ON ps.process_name = gp.process_name
        GROUP BY ps.session_id
    ) g ON g.session_id = s.session_id
    WHERE s.end_time IS NOT NULL
) b
CROSS JOIN score_config c
WHERE b.session_sec >= c.min_session_sec;

-- Average score per employee (the boss dashboard will read this)
CREATE OR REPLACE VIEW employee_scores AS
SELECT
    employee_id,
    COUNT(*)                           AS sessions_counted,
    ROUND(AVG(productivity_score), 1)  AS avg_score,
    MIN(productivity_score)            AS worst_score,
    MAX(productivity_score)            AS best_score
FROM session_scores
GROUP BY employee_id;
