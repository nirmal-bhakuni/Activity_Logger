-- Activity Monitor Database Schema
-- Run this file to set up the database from scratch

CREATE DATABASE IF NOT EXISTS activity_monitor;
USE activity_monitor;

CREATE TABLE IF NOT EXISTS sessions (
    session_id INT AUTO_INCREMENT PRIMARY KEY,
    start_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    end_time DATETIME NULL,
    total_idle_seconds INT DEFAULT 0,
    employee_id VARCHAR(50),
    machine_name VARCHAR(100)
);

CREATE TABLE IF NOT EXISTS process_snapshots (
    snapshot_id INT AUTO_INCREMENT PRIMARY KEY,
    session_id INT NOT NULL,
    process_name VARCHAR(255),
    pid INT,
    cpu_usage FLOAT,
    memory_usage_mb FLOAT,
    is_idle BOOLEAN,
    log_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (session_id) REFERENCES sessions(session_id)
);