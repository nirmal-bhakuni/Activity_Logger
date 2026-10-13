-- ============================================================
-- 04_remote_user.sql  -  OPTIONAL: let other PCs connect to this database
-- ============================================================
-- Only needed when agents run on OTHER machines than the MySQL server.
-- Run this on the SERVER machine. Before running, change CHANGE_ME_PASSWORD.
-- Never commit a real password to Git.
--
-- ALSO NEEDED on the server (these are not SQL):
--   1. In my.ini under [mysqld] add:   bind-address=0.0.0.0
--      then restart:   Restart-Service -Name "MySQL80"     (admin PowerShell)
--   2. Open the firewall (admin PowerShell):
--      New-NetFirewallRule -DisplayName "MySQL Remote Access" -Direction Inbound -Protocol TCP -LocalPort 3306 -Action Allow
--   3. On each agent PC, put the server's IP and this user in config.ini
--
-- Note: '%' means "connect from any computer". Fine for a college demo;
-- in real use, restrict it to known addresses.
-- ============================================================

CREATE USER IF NOT EXISTS 'monitor_user'@'%' IDENTIFIED BY 'CHANGE_ME_PASSWORD';

-- Access to this one database only, not all of MySQL
GRANT ALL PRIVILEGES ON activity_monitor.* TO 'monitor_user'@'%';
FLUSH PRIVILEGES;
