-- ============================================================
-- 01_schema.sql  -  Tables for the Activity Monitor project
-- ============================================================
-- WHAT THIS FILE DOES
--   Creates the database and all tables. Safe to run more than once
--   (it uses IF NOT EXISTS, so existing data is never deleted).
--
-- TABLE OVERVIEW
--   sessions            one row per monitoring run (start/end, who, which PC)
--   process_snapshots   many rows per session: one row per app per 5-second check
--                       (linked to sessions by a foreign key)
--   game_processes      list of process names counted as "games" (used for score)
--   score_config        weights/settings for the productivity score
--
-- RUN ORDER: 01_schema -> 02_seed_data -> 03_views
-- ============================================================

CREATE DATABASE IF NOT EXISTS activity_monitor;
USE activity_monitor;

-- One row per monitoring run of activity_monitor.exe
CREATE TABLE IF NOT EXISTS sessions (
    session_id         INT AUTO_INCREMENT PRIMARY KEY,
    start_time         DATETIME DEFAULT CURRENT_TIMESTAMP,
    end_time           DATETIME NULL,            -- NULL = session still running or was never closed
    total_idle_seconds INT DEFAULT 0,
    employee_id        VARCHAR(50),              -- from config.ini, e.g. EMP001
    machine_name       VARCHAR(100)              -- detected automatically from the PC name
);

-- One row per visible application per check cycle
CREATE TABLE IF NOT EXISTS process_snapshots (
    snapshot_id     INT AUTO_INCREMENT PRIMARY KEY,
    session_id      INT NOT NULL,
    process_name    VARCHAR(255),
    pid             INT,
    cpu_usage       FLOAT,                       -- percent
    memory_usage_mb FLOAT,
    is_idle         BOOLEAN,                     -- 1 if the user had been idle past the threshold
    log_time        DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (session_id) REFERENCES sessions(session_id)
);

-- Process names that count as games when the productivity score is calculated
CREATE TABLE IF NOT EXISTS game_processes (
    process_name VARCHAR(255) PRIMARY KEY
);

-- Settings for the productivity score (one row, id = 1)
CREATE TABLE IF NOT EXISTS score_config (
    id                    INT PRIMARY KEY,
    game_weight           DOUBLE NOT NULL,       -- penalty per % of session spent gaming
    idle_weight           DOUBLE NOT NULL,       -- penalty per % of session spent idle
    min_session_sec       INT NOT NULL,          -- shorter sessions are not scored
    snapshot_interval_sec INT NOT NULL           -- must match SNAPSHOT_INTERVAL_SECONDS in activity_monitor.cpp
);
