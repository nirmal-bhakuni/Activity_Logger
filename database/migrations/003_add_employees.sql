USE activity_monitor;

CREATE TABLE IF NOT EXISTS employees (
    employee_id   VARCHAR(50) PRIMARY KEY,
    employee_name VARCHAR(100) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    role          ENUM('employee', 'boss') NOT NULL,
    active        BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_employees_role ON employees(role);