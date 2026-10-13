-- ============================================================
-- 02_seed_data.sql  -  Starting data for the score settings
-- ============================================================
-- WHAT THIS FILE DOES
--   Inserts the default game list and default score weights.
--   Safe to run more than once (INSERT IGNORE skips rows that exist).
--
-- HOW TO CHANGE THINGS LATER (no recompiling needed)
--   Add a game:      INSERT INTO game_processes VALUES ('GameName.exe');
--   Change weights:  UPDATE score_config SET game_weight = 1.5 WHERE id = 1;
-- ============================================================

USE activity_monitor;

-- Add the exact .exe names your monitor records. To see what it recorded:
--   SELECT DISTINCT process_name FROM process_snapshots;
INSERT IGNORE INTO game_processes (process_name) VALUES
    ('RobloxPlayerBeta.exe');

-- Score = 100 - game_weight * (% time gaming) - idle_weight * (% time idle)
-- These weights are a policy choice, not a scientific formula.
INSERT IGNORE INTO score_config
    (id, game_weight, idle_weight, min_session_sec, snapshot_interval_sec)
VALUES
    (1, 1.0, 0.5, 30, 5);
