-- ============================================================
-- migrations/002_create_ai_insights.sql  -  stores AI-written insights
-- ============================================================
-- RUN THIS ONCE on a database created before this change.
-- (A brand-new database made with 01_schema.sql already has the table.)
--
-- Why a table: the dashboard / boss reads the SAVED insight instead of calling
-- the AI on every page view. That is faster, free of rate limits, and it keeps
-- an audit trail: input_json is EXACTLY what was sent to Gemini (no names, no
-- employee ID), output_json is exactly what came back.
--
-- Run with:  mysql -u root -p -e "source database/migrations/002_create_ai_insights.sql"
-- ============================================================

USE activity_monitor;

CREATE TABLE IF NOT EXISTS ai_insights (
    insight_id    INT AUTO_INCREMENT PRIMARY KEY,
    employee_id   VARCHAR(50) NOT NULL,
    generated_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    model_name    VARCHAR(100),
    sessions_used INT,
    input_json    JSON,          -- what was sent to the AI
    output_json   JSON,          -- what the AI answered (after our checks)
    is_focused BOOLEAN NULL DEFAULT NULL,
    INDEX idx_insight_employee (employee_id, generated_at)
);
