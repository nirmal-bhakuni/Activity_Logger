-- ============================================================
-- setup_all.sql  -  Runs files 01, 02 and 03 in order
-- ============================================================
-- Run this from the PROJECT ROOT folder (the folder that contains "database"):
--   mysql -u root -p -e "source database/setup_all.sql"
-- (04_remote_user.sql is optional and is not included here.)
-- ============================================================

source database/01_schema.sql
source database/02_seed_data.sql
source database/03_views.sql
