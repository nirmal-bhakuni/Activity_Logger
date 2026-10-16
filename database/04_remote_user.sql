-- ============================================================
-- 04_remote_user.sql  -  Creates the database user the agent logs in with
-- ============================================================
-- Run this on the SERVER PC (the one running MySQL). Before running,
-- change CHANGE_ME_PASSWORD (in all four places). Never commit a real password.
--
-- WHY TWO ACCOUNTS
--   MySQL treats 'monitor_user'@'localhost' and 'monitor_user'@'%' as two
--   separate accounts.
--     @'localhost'  used when the agent runs on this same PC (host=localhost)
--     @'%'          used when an agent on ANOTHER PC connects over the network
--   If you only run the agent on the server PC, the first account is enough.
--
-- FOR OTHER PCs YOU ALSO NEED (these are not SQL):
--   1. In my.ini under [mysqld] add:   bind-address=0.0.0.0
--      then restart:   Restart-Service -Name "MySQL80"     (admin PowerShell)
--   2. Open the firewall (admin PowerShell):
--      New-NetFirewallRule -DisplayName "MySQL Remote Access" -Direction Inbound -Protocol TCP -LocalPort 3306 -Action Allow
--   3. On each agent PC, put the server's IP and this user in config.ini
--
-- Note: '%' means "connect from any computer". Fine for a college demo;
-- in real use, restrict it to known addresses.
--
-- CHANGING A PASSWORD LATER
--   CREATE USER IF NOT EXISTS does NOT change the password of an existing user.
--   Use ALTER USER for that (the lines below do this, so the file can be re-run).
-- ============================================================

-- Account for an agent running on this same PC
CREATE USER IF NOT EXISTS 'monitor_user'@'localhost' IDENTIFIED BY 'CHANGE_ME_PASSWORD';
ALTER USER 'monitor_user'@'localhost' IDENTIFIED BY 'CHANGE_ME_PASSWORD';
GRANT ALL PRIVILEGES ON activity_monitor.* TO 'monitor_user'@'localhost';

-- Account for agents on other PCs
CREATE USER IF NOT EXISTS 'monitor_user'@'%' IDENTIFIED BY 'CHANGE_ME_PASSWORD';
ALTER USER 'monitor_user'@'%' IDENTIFIED BY 'CHANGE_ME_PASSWORD';
GRANT ALL PRIVILEGES ON activity_monitor.* TO 'monitor_user'@'%';

-- Access is limited to this one database, not all of MySQL
FLUSH PRIVILEGES;