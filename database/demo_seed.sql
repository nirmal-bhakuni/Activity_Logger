USE activity_monitor;

START TRANSACTION;

-- Remove previous demo data if it exists
DELETE FROM process_snapshots
WHERE session_id IN (
    SELECT session_id FROM sessions
    WHERE employee_id IN ('EMP_RAGHAV', 'EMP_ATHARVA', 'EMP_DHRUV')
);

DELETE FROM sessions
WHERE employee_id IN ('EMP_RAGHAV', 'EMP_ATHARVA', 'EMP_DHRUV');

DROP PROCEDURE IF EXISTS create_demo_data;

DELIMITER //

CREATE PROCEDURE create_demo_data()
BEGIN
    DECLARE v_person INT DEFAULT 1;
    DECLARE v_session INT;
    DECLARE v_total_sessions INT;
    DECLARE v_employee_id VARCHAR(50);
    DECLARE v_machine_name VARCHAR(100);
    DECLARE v_start DATETIME;
    DECLARE v_session_id INT;

    WHILE v_person <= 3 DO

        IF v_person = 1 THEN
            SET v_employee_id = 'EMP_RAGHAV';
            SET v_machine_name = 'Raghav';
            SET v_total_sessions = 4;
        ELSEIF v_person = 2 THEN
            SET v_employee_id = 'EMP_ATHARVA';
            SET v_machine_name = 'Atharva';
            SET v_total_sessions = 6;
        ELSE
            SET v_employee_id = 'EMP_DHRUV';
            SET v_machine_name = 'Dhruv';
            SET v_total_sessions = 8;
        END IF;

        SET v_session = 1;

        WHILE v_session <= v_total_sessions DO

            SET v_start = DATE_ADD(
                '2026-11-15 08:00:00',
                INTERVAL (((v_person - 1) * 1000) + (v_session * 20)) MINUTE
            );

            INSERT INTO sessions
                (start_time, end_time, total_idle_seconds,
                 employee_id, machine_name)
            VALUES
                (
                    v_start,
                    DATE_ADD(v_start, INTERVAL 60 SECOND),
                    IF(v_person = 2 AND MOD(v_session, 3) = 0, 20, 5),
                    v_employee_id,
                    v_machine_name
                );

            SET v_session_id = LAST_INSERT_ID();

            INSERT INTO process_snapshots
                (session_id, process_name, pid, cpu_usage,
                 memory_usage_mb, is_idle, is_focused, log_time)
            VALUES
                (
                    v_session_id,
                    IF(v_person = 2 AND MOD(v_session, 2) = 0,
                       'RobloxPlayerBeta.exe', 'chrome.exe'),
                    10000 + v_session,
                    12.5 + v_session,
                    400.0 + v_session * 10,
                    0,
                    1,
                    DATE_ADD(v_start, INTERVAL 5 SECOND)
                ),
                (
                    v_session_id,
                    IF(v_person = 1, 'code.exe', 'chrome.exe'),
                    11000 + v_session,
                    20.0 + v_session,
                    500.0 + v_session * 10,
                    0,
                    1,
                    DATE_ADD(v_start, INTERVAL 10 SECOND)
                ),
                (
                    v_session_id,
                    IF(v_person = 3, 'code.exe', 'chrome.exe'),
                    12000 + v_session,
                    8.0 + v_session,
                    350.0 + v_session * 10,
                    IF(v_person = 2 AND MOD(v_session, 3) = 0, 1, 0),
                    0,
                    DATE_ADD(v_start, INTERVAL 15 SECOND)
                ),
                (
                    v_session_id,
                    'chrome.exe',
                    13000 + v_session,
                    6.0 + v_session,
                    380.0 + v_session * 10,
                    0,
                    1,
                    DATE_ADD(v_start, INTERVAL 20 SECOND)
                );

            SET v_session = v_session + 1;
        END WHILE;

        SET v_person = v_person + 1;
    END WHILE;
END//

DELIMITER ;

CALL create_demo_data();

DROP PROCEDURE create_demo_data;

COMMIT;