-- ============================================================
-- migrations/001_add_is_focused.sql  -  adds focused-window tracking
-- ============================================================
-- RUN THIS ONCE on a database that was created BEFORE this change.
-- (A brand-new database made with 01_schema.sql already has the column.)
-- Running it twice gives "Duplicate column name" - that is harmless.
--
-- is_focused:
--   1    = this app's window had keyboard focus when the check ran
--   0    = it was open but not in focus
--   NULL = recorded before focus tracking existed (unknown, NOT "not focused")
-- Old rows stay NULL so they are never mistaken for real "not focused" data.
--
-- Run with:  mysql -u root -p -e "source database/migrations/001_add_is_focused.sql"
-- ============================================================

USE activity_monitor;

ALTER TABLE process_snapshots
    ADD COLUMN is_focused BOOLEAN NULL DEFAULT NULL AFTER is_idle;
