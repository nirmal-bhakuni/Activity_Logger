"""Activity Logger dashboard.

Run from the repository root with ``python -m web.app``. Database credentials
are read from config.ini, while the Flask session key must come from
WEB_SECRET_KEY (or is generated per process for local development).
"""
import configparser
import json
import os
import secrets
import sys
from functools import wraps
from pathlib import Path

import mysql.connector
from flask import Flask, flash, g, redirect, render_template, request, session, url_for
from werkzeug.security import check_password_hash
from ai import gemini_client, insights

ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = Path(os.environ.get("ACTIVITY_CONFIG", ROOT / "config.ini"))


def load_database_config():
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(CONFIG_PATH):
        raise RuntimeError("config.ini was not found. Copy config.ini.example and edit it.")
    if "database" not in config:
        raise RuntimeError("config.ini has no [database] section.")
    return config["database"]


def create_app():
    app = Flask(__name__, template_folder="templates", static_folder="static")
    app.config.update(
        SECRET_KEY=os.environ.get("WEB_SECRET_KEY") or secrets.token_hex(32),
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Lax",
        SESSION_COOKIE_SECURE=os.environ.get("WEB_COOKIE_SECURE", "").lower() == "true",
    )
    app.config["DATABASE_CONFIG"] = load_database_config

    @app.before_request
    def ensure_csrf_token():
        session.setdefault("_csrf_token", secrets.token_urlsafe(24))

    @app.teardown_appcontext
    def close_db(_error=None):
        connection = g.pop("db", None)
        if connection is not None and connection.is_connected():
            connection.close()

    @app.context_processor
    def inject_template_helpers():
        return {"csrf_token": lambda: session.get("_csrf_token", "")}

    def get_db():
        if "db" not in g:
            db = app.config["DATABASE_CONFIG"]()
            g.db = mysql.connector.connect(
                host=db.get("host", "localhost"),
                user=db.get("user"),
                password=db.get("password", ""),
                database=db.get("database", "activity_monitor"),
                port=int(db.get("port", "3306")),
            )
        return g.db

    def query(sql, params=()):
        cursor = get_db().cursor(dictionary=True)
        try:
            cursor.execute(sql, params)
            return cursor.fetchall()
        finally:
            cursor.close()

    def post_is_valid():
        return request.form.get("_csrf_token", "") == session.get("_csrf_token")

    def login_required(view):
        @wraps(view)
        def wrapped(*args, **kwargs):
            if not session.get("employee_id"):
                return redirect(url_for("login", next=request.path))
            return view(*args, **kwargs)
        return wrapped

    def role_required(role):
        def decorator(view):
            @wraps(view)
            @login_required
            def wrapped(*args, **kwargs):
                if session.get("role") != role:
                    flash("You are not allowed to view that page.", "error")
                    return redirect(url_for("dashboard"))
                return view(*args, **kwargs)
            return wrapped
        return decorator

    @app.route("/")
    def index():
        return redirect(url_for("dashboard")) if session.get("employee_id") else redirect(url_for("login"))

    @app.route("/login", methods=("GET", "POST"))
    def login():
        if session.get("employee_id"):
            return redirect(url_for("dashboard"))
        if request.method == "POST":
            if not post_is_valid():
                flash("Your form expired. Please try again.", "error")
            else:
                employee_id = request.form.get("employee_id", "").strip()
                password = request.form.get("password", "")
                try:
                    rows = query(
                        "SELECT employee_id, employee_name, password_hash, role "
                        "FROM employees WHERE employee_id = %s AND active = TRUE",
                        (employee_id,),
                    )
                except mysql.connector.Error:
                    app.logger.exception("Database error during login")
                    flash("The database is unavailable. Try again later.", "error")
                    rows = []
                if rows and check_password_hash(rows[0]["password_hash"], password):
                    user = rows[0]
                    session.clear()
                    session["_csrf_token"] = secrets.token_urlsafe(24)
                    session.update(
                        employee_id=user["employee_id"],
                        employee_name=user["employee_name"],
                        role=user["role"],
                    )
                    return redirect(url_for("dashboard"))
                flash("Invalid employee ID or password.", "error")
        return render_template("login.html")

    @app.post("/logout")
    @login_required
    def logout():
        if post_is_valid():
            session.clear()
        else:
            flash("Your form expired. Please try again.", "error")
        return redirect(url_for("login"))

    @app.get("/dashboard")
    @login_required
    def dashboard():
        return redirect(url_for("boss_dashboard") if session["role"] == "boss" else url_for("employee_dashboard"))

    def load_insights(employee_id):
        rows = query(
            "SELECT insight_id, generated_at, model_name, output_json "
            "FROM ai_insights WHERE employee_id = %s ORDER BY generated_at DESC, insight_id DESC LIMIT 10",
            (employee_id,),
        )
        for row in rows:
            value = row.get("output_json")
            if isinstance(value, (bytes, bytearray)):
                value = value.decode("utf-8")
            if isinstance(value, str):
                try:
                    row["output"] = json.loads(value)
                except (TypeError, ValueError):
                    row["output"] = {}
            else:
                row["output"] = value or {}
        return rows

    def load_employee_data(employee_id):
        employee_rows = query(
            "SELECT employee_id, employee_name FROM employees WHERE employee_id = %s AND active = TRUE",
            (employee_id,),
        )
        employee = employee_rows[0] if employee_rows else {
            "employee_id": employee_id, "employee_name": employee_id
        }
        scores = query(
            "SELECT session_id, start_time, session_sec, game_sec, idle_sec, productivity_score "
            "FROM session_scores WHERE employee_id = %s ORDER BY start_time DESC LIMIT 50",
            (employee_id,),
        )
        return employee, scores, load_insights(employee_id)

    @app.post("/insights/generate")
    @login_required
    def generate_insight():
        if not post_is_valid():
            flash("Your form expired. Please try again.", "error")
            return redirect(url_for("dashboard"))
        employee_id = session["employee_id"]
        if session.get("role") == "boss":
            employee_id = request.form.get("employee_id", "").strip()
            if not employee_id:
                flash("Choose an employee before generating an insight.", "error")
                return redirect(url_for("boss_dashboard"))
            valid = query(
                "SELECT employee_id FROM employees WHERE employee_id = %s AND active = TRUE",
                (employee_id,),
            )
            if not valid:
                flash("That employee could not be found.", "error")
                return redirect(url_for("boss_dashboard"))
        try:
            key, model = insights.gemini_client.load_settings(str(CONFIG_PATH))
            insights.generate_insight(
                get_db(), employee_id,
                model=model,
                key=key,
            )
            flash("A new AI insight was generated and saved.", "success")
        except (mysql.connector.Error, gemini_client.GeminiError) as error:
            app.logger.exception("Could not generate AI insight")
            flash(str(error), "error")
        if session.get("role") == "boss":
            return redirect(url_for("employee_detail", employee_id=employee_id))
        return redirect(url_for("employee_dashboard"))

    @app.get("/employee")
    @role_required("employee")
    def employee_dashboard():
        try:
            employee, scores, insights = load_employee_data(session["employee_id"])
        except mysql.connector.Error:
            app.logger.exception("Database error loading employee dashboard")
            flash("Some dashboard data could not be loaded.", "error")
            employee, scores, insights = {"employee_name": session.get("employee_name")}, [], []
        return render_template("employee_dashboard.html", employee=employee, scores=scores, insights=insights)

    @app.get("/boss")
    @role_required("boss")
    def boss_dashboard():
        try:
            employees = query(
                "SELECT e.employee_id, e.employee_name, e.role, "
                "COALESCE(s.sessions_counted, 0) AS sessions_counted, "
                "s.avg_score, s.worst_score, s.best_score "
                "FROM employees e LEFT JOIN employee_scores s ON s.employee_id = e.employee_id "
                "WHERE e.active = TRUE ORDER BY e.employee_name, e.employee_id"
            )
        except mysql.connector.Error:
            app.logger.exception("Database error loading boss dashboard")
            flash("Employee summaries could not be loaded.", "error")
            employees = []
        return render_template("boss_dashboard.html", employees=employees)

    @app.get("/boss/employees/<employee_id>")
    @role_required("boss")
    def employee_detail(employee_id):
        try:
            employee, scores, insights = load_employee_data(employee_id)
        except mysql.connector.Error:
            app.logger.exception("Database error loading employee detail")
            flash("Employee data could not be loaded.", "error")
            employee, scores, insights = {"employee_id": employee_id, "employee_name": employee_id}, [], []
        return render_template("employee_detail.html", employee=employee, scores=scores, insights=insights)

    return app


app = create_app()

if __name__ == "__main__":
    app.run(host=os.environ.get("FLASK_HOST", "127.0.0.1"), port=int(os.environ.get("FLASK_PORT", "5000")))
