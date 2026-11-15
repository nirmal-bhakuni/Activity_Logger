"""Create or update a dashboard account without putting credentials in source.

Example (run from the repository root):
    python web/bootstrap_account.py --employee-id EMP001 --name "A Name" --role employee
The password is entered privately with getpass unless --password-stdin is used.
"""
import argparse
import configparser
import getpass
import os
import sys
from pathlib import Path

import mysql.connector
from werkzeug.security import generate_password_hash

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from web.app import load_database_config  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description="Create an Activity Logger web account.")
    parser.add_argument("--employee-id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--role", choices=("employee", "boss"), required=True)
    parser.add_argument("--password-stdin", action="store_true")
    args = parser.parse_args()
    password = sys.stdin.readline().rstrip("\r\n") if args.password_stdin else getpass.getpass("Password: ")
    if len(password) < 12:
        parser.error("Password must be at least 12 characters.")
    db = load_database_config()
    connection = mysql.connector.connect(
        host=db.get("host", "localhost"), user=db.get("user"),
        password=db.get("password", ""), database=db.get("database", "activity_monitor"),
        port=int(db.get("port", "3306")),
    )
    try:
        cursor = connection.cursor()
        cursor.execute(
            "INSERT INTO employees (employee_id, employee_name, password_hash, role, active) "
            "VALUES (%s, %s, %s, %s, TRUE) "
            "ON DUPLICATE KEY UPDATE employee_name = VALUES(employee_name), "
            "password_hash = VALUES(password_hash), role = VALUES(role), active = TRUE",
            (args.employee_id, args.name, generate_password_hash(password), args.role),
        )
        connection.commit()
        print("Account saved for %s (%s)." % (args.employee_id, args.role))
    finally:
        connection.close()


if __name__ == "__main__":
    main()
