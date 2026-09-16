#!/usr/bin/env python3
"""
Jockey Windows Server
- Starts / connects to jockey.exe --serve on a port
- Serves the same Jockey Console UI as server.py
- Same 147 functions, same bytecode protocol, same HTML
"""
import struct
import socket
import os
import sys
import subprocess
import platform
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import parse_qs

# ── paths ───────────────────────────────────────────────────────────────
HERE      = os.path.dirname(os.path.abspath(__file__))
ROOT      = os.path.dirname(HERE)                        # sih26148/
JOCKEY_EXE = os.path.join(ROOT, "jockey_agent", "windows", "jockey.exe")
LANG_DIR  = os.path.join(ROOT, "jocky_language")

AGENT_HOST = os.environ.get("JKY_AGENT_HOST", "127.0.0.1")
AGENT_PORT = int(os.environ.get("JKY_AGENT_PORT", "9090"))
WEB_PORT   = int(os.environ.get("JKY_WEB_PORT",   "8080"))

# ── function reference (same as server.py) ──────────────────────────────
REFERENCE = [
    ("Language", [
        ("print(...)",               "Write values to output"),
        ("len(x)",                   "Length of string, array, or dict"),
        ("str(x)",                   "Convert to string"),
        ("int(x)",                   "Convert to integer"),
        ("float(x)",                 "Convert to float"),
        ("bool(x)",                  "Convert to 0 or 1"),
        ("list(...)",                "Build an array"),
        ("dict(...)",                "Build a dict"),
        ("range(stop)",              "0 to stop-1"),
        ("range(start, stop)",       "start to stop-1"),
        ("range(start, stop, step)", "Step range"),
        ("abs(x)",                   "Absolute value"),
        ("min(...)",                 "Smallest value"),
        ("max(...)",                 "Largest value"),
        ("type(x)",                  "Type name as string"),
        ("supports(name)",           "True if function exists"),
    ]),
    ("System", [
        ("sysinfo()",                "Host info dict"),
        ("system_platform()",        "linux / windows / macos"),
        ("system_hostname()",        "Hostname"),
        ("system_username()",        "Current user"),
        ("system_kernel_version()",  "Kernel release"),
        ("system_arch()",            "Architecture"),
        ("system_cpus()",            "CPU count"),
        ("system_uptime()",          "Seconds since boot"),
        ("system_memory()",          "Memory dict"),
        ("system_info()",            "Full system dict"),
        ("time()",                   "Unix epoch seconds"),
        ("sleep(ms)",                "Sleep milliseconds"),
        ("env([name])",              "Env var or all env"),
        ("exec(cmd)",                "Run shell, return stdout"),
    ]),
    ("Filesystem", [
        ("cwd()",                    "Current directory"),
        ("chdir(path)",              "Change directory"),
        ("list_dir([path])",         "Array of names"),
        ("read_file(path)",          "Bytes of file"),
        ("fs_write(path, data)",     "Write file"),
        ("file_exists(path)",        "Test existence"),
        ("file_size(path)",          "Size in bytes"),
        ("fs_stat(path)",            "Stat dict"),
        ("hide_file(path)",          "Kernel-level hide"),
    ]),
    ("Process", [
        ("getpid()",                 "Our PID"),
        ("getppid()",                "Parent PID"),
        ("procs()",                  "Array of {pid, name}"),
        ("kill(pid)",                "Send SIGTERM"),
        ("proc_info(pid)",           "Full status dict"),
        ("proc_modules(pid)",        "Loaded modules"),
        ("proc_connections(pid)",    "TCP connections"),
    ]),
    ("Memory", [
        ("mem_read(pid, addr, len)", "Read process memory"),
        ("mem_write(pid, addr, d)",  "Write process memory"),
        ("mem_strings(pid, [min])",  "Extract strings"),
    ]),
    ("Kernel", [
        ("hide_pid(pid)",            "Hide process"),
        ("unhide_pid(pid)",          "Reveal process"),
        ("kill_pid(pid)",            "Kernel-level kill"),
        ("get_root()",               "Escalate privileges"),
        ("kernel_modules()",         "Loaded drivers"),
        ("disable_etw()",            "Disable ETW"),
    ]),
    ("Network", [
        ("net_connect(host, port)",  "Open TCP socket"),
        ("net_send(fd, data)",       "Send bytes"),
        ("net_recv(fd, maxlen)",     "Receive bytes"),
        ("net_close(fd)",            "Close socket"),
        ("netstat()",                "TCP connections"),
        ("net_resolve(host)",        "DNS resolution"),
    ]),
    ("Credentials", [
        ("cred_users()",             "Local users"),
        ("cred_sessions()",          "Active sessions"),
        ("cred_token()",             "Token info"),
    ]),
    ("Anti-forensics", [
        ("anti_debug()",             "Detect debugger"),
        ("anti_vm()",                "Detect VM"),
        ("anti_sandbox()",           "Detect sandbox"),
        ("anti_edr()",               "Detect EDR"),
    ]),
    ("Crypto", [
        ("md5(data)",                "MD5 hex digest"),
        ("sha256(data)",             "SHA-256 hex digest"),
        ("xor(data, key)",           "XOR bytes"),
        ("b64_encode(data)",         "Base64 encode"),
        ("b64_decode(s)",            "Base64 decode"),
    ]),
    ("Data", [
        ("data_hex_dump(data)",      "Hex string"),
        ("data_timestamp()",         "Unix epoch"),
        ("data_uuid()",              "Random UUID v4"),
        ("data_random_bytes(n)",     "N random bytes"),
    ]),
    ("UI / Debug", [
        ("display_clear()",          "Clear terminal"),
        ("display_text(msg)",        "Write without newline"),
        ("play_beep()",              "Terminal bell"),
        ("trace(...)",               "Write to stderr"),
    ]),
]


def render_reference():
    out = []
    for cat, fns in REFERENCE:
        rows = "".join(
            f'<button class="fn" onclick="insertFn(this)" data-fn="{name}">'
            f'<code>{name}</code><span class="fn-doc">{doc}</span></button>'
            for name, doc in fns
        )
        out.append(
            f'<details class="ref-group">'
            f'<summary><span>{cat}</span>'
            f'<span class="ref-count">{len(fns)}</span></summary>'
            f'<div class="ref-body">{rows}</div>'
            f'</details>'
        )
    return "\n".join(out)


# ── HTML (same UI as server.py) ────────────────────────────────────────
HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Jockey Console</title>
<style>
:root {{
  --border:        #dcdcde;
  --border-strong: #bfbfc3;
  --text:          #2e2e2e;
  --text-muted:    #666;
  --bg:            #ffffff;
  --bg-subtle:     #fafafa;
  --bg-hover:      #f0f0f0;
  --purple:        #6666c4;
  --purple-hover:  #5252a8;
  --green:         #217645;
  --red:           #dd2b0e;
  --blue:          #1f75cb;
}}
* {{ box-sizing: border-box; }}
html, body {{
  margin: 0; padding: 0; height: 100%;
  background: var(--bg); color: var(--text);
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
               "Helvetica Neue", Arial, sans-serif;
  font-size: 14px; line-height: 1.5;
}}
code, pre, textarea {{
  font-family: "SF Mono", Monaco, "Cascadia Code", "Roboto Mono",
               Consolas, monospace;
}}
header {{
  height: 48px; background: var(--bg);
  border-bottom: 1px solid var(--border);
  display: flex; align-items: center;
  padding: 0 16px; gap: 16px;
}}
.brand {{
  font-weight: 600; font-size: 15px;
  letter-spacing: 0.02em; color: var(--text);
}}
.brand::before {{
  content: ""; display: inline-block;
  width: 20px; height: 20px;
  margin-right: 8px; background: var(--purple);
  border-radius: 4px; vertical-align: middle;
}}
.spacer {{ flex: 1; }}
.pill {{
  display: inline-flex; align-items: center; gap: 6px;
  padding: 3px 10px; border-radius: 100px;
  font-size: 12px; font-weight: 500;
  border: 1px solid var(--border);
  background: var(--bg-subtle);
  color: var(--text-muted);
}}
.pill .dot {{
  width: 6px; height: 6px;
  border-radius: 50%; background: #bbb;
}}
.pill.on   .dot {{ background: var(--green); }}
.pill.off  .dot {{ background: var(--red); }}
.pill.on   {{ color: var(--green); border-color: #c5e3d1; background: #f0faf4; }}
.pill.off  {{ color: var(--red);   border-color: #f2c9c0; background: #fdf3f1; }}
.app {{
  display: grid;
  grid-template-columns: 300px 1fr;
  height: calc(100vh - 48px);
}}
aside {{
  background: var(--bg-subtle);
  border-right: 1px solid var(--border);
  overflow-y: auto; padding: 12px 0;
}}
main {{ overflow-y: auto; padding: 24px 32px 48px; }}
.aside-title {{
  padding: 0 16px 8px;
  font-size: 11px; font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--text-muted);
}}
.ref-filter {{
  margin: 0 12px 12px;
  padding: 6px 10px;
  width: calc(100% - 24px);
  border: 1px solid var(--border);
  border-radius: 4px;
  font: inherit; font-size: 12px;
  background: var(--bg);
}}
.ref-group {{ border-top: 1px solid var(--border); }}
.ref-group:first-of-type {{ border-top: none; }}
.ref-group summary {{
  padding: 8px 16px;
  font-size: 13px; font-weight: 600;
  cursor: pointer; list-style: none;
  user-select: none;
  display: flex; align-items: center;
  justify-content: space-between;
}}
.ref-group summary::-webkit-details-marker {{ display: none; }}
.ref-group summary::before {{
  content: "\\25B8"; display: inline-block;
  width: 12px; margin-right: 4px;
  color: var(--text-muted);
  transition: transform 0.1s;
}}
.ref-group[open] summary::before {{ transform: rotate(90deg); }}
.ref-group summary:hover {{ background: var(--bg-hover); }}
.ref-count {{
  font-size: 11px; font-weight: 500;
  color: var(--text-muted);
  background: var(--bg-hover);
  padding: 1px 6px; border-radius: 8px;
}}
.ref-body {{ padding: 0 8px 8px; }}
.fn {{
  display: block; width: 100%;
  text-align: left; padding: 6px 8px;
  border: none; background: transparent;
  border-radius: 4px; cursor: pointer;
  color: inherit; font: inherit;
}}
.fn:hover {{ background: var(--bg-hover); }}
.fn code {{
  display: block; font-size: 12px;
  color: var(--blue); font-weight: 500;
}}
.fn-doc {{
  display: block; font-size: 11px;
  color: var(--text-muted);
  margin-top: 1px;
}}
.card {{
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  margin-bottom: 20px;
  overflow: hidden;
}}
.card-header {{
  padding: 12px 16px;
  border-bottom: 1px solid var(--border);
  display: flex; align-items: center; gap: 12px;
  background: var(--bg-subtle);
}}
.card-title {{ font-size: 14px; font-weight: 600; }}
.card-body {{ padding: 16px; }}
textarea.editor {{
  width: 100%; min-height: 260px;
  padding: 14px 16px;
  border: none; outline: none;
  resize: vertical;
  background: var(--bg); color: var(--text);
  font-size: 13px; line-height: 1.6;
  tab-size: 4;
}}
.editor-wrap {{
  border: 1px solid var(--border);
  border-radius: 4px; overflow: hidden;
}}
.editor-toolbar {{
  display: flex; gap: 8px; margin-top: 12px;
  align-items: center;
}}
.btn {{
  display: inline-flex; align-items: center; gap: 6px;
  padding: 6px 14px; border-radius: 4px;
  font-size: 13px; font-weight: 500;
  cursor: pointer; border: 1px solid transparent;
  font-family: inherit; line-height: 1.4;
}}
.btn-primary {{
  background: var(--purple); color: #fff;
  border-color: var(--purple);
}}
.btn-primary:hover {{ background: var(--purple-hover); border-color: var(--purple-hover); }}
.btn-primary:disabled {{ opacity: 0.6; cursor: not-allowed; }}
.btn-default {{
  background: var(--bg); color: var(--text);
  border-color: var(--border-strong);
}}
.btn-default:hover {{ background: var(--bg-hover); }}
.output {{
  background: #1e1e2e;
  color: #cdd6f4;
  padding: 16px;
  font-size: 13px; line-height: 1.6;
  white-space: pre-wrap; word-break: break-word;
  min-height: 140px; max-height: 500px;
  overflow-y: auto; margin: 0;
}}
.output.ok    {{ border-left: 3px solid var(--green); }}
.output.err   {{ border-left: 3px solid var(--red); }}
.output.info  {{ border-left: 3px solid var(--blue); }}
.output-meta {{
  padding: 8px 16px;
  border-top: 1px solid var(--border);
  font-size: 12px; color: var(--text-muted);
  background: var(--bg-subtle);
  display: flex; gap: 16px;
}}
.output-meta .k {{ color: var(--text-muted); }}
.output-meta .v {{ color: var(--text); font-weight: 500; }}
.status-grid {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
  gap: 12px; margin-bottom: 20px;
}}
.stat {{
  padding: 12px 16px;
  border: 1px solid var(--border);
  border-radius: 6px; background: var(--bg);
}}
.stat-label {{
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--text-muted); font-weight: 600;
}}
.stat-value {{
  margin-top: 4px;
  font-size: 16px; font-weight: 600;
  color: var(--text);
}}
.stat-value.ok   {{ color: var(--green); }}
.stat-value.err  {{ color: var(--red); }}
</style>
</head>
<body>
<header>
  <div class="brand">Jockey Console</div>
  <div class="spacer"></div>
  <span class="pill {agent_class}">
    <span class="dot"></span>
    <span>agent {agent_status}</span>
  </span>
  <span class="pill {kmod_class}">
    <span class="dot"></span>
    <span>kernel {kmod_status}</span>
  </span>
</header>
<div class="app">
  <aside>
    <div class="aside-title">Reference &middot; {fn_total} functions</div>
    <input class="ref-filter" id="ref-filter" placeholder="filter&hellip;"
           oninput="filterRef(this.value)">
    <div id="ref-list">{reference}</div>
  </aside>
  <main>
    <div class="status-grid">
      <div class="stat">
        <div class="stat-label">Target</div>
        <div class="stat-value">{target}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Agent</div>
        <div class="stat-value {agent_class_value}">{agent_status}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Kernel module</div>
        <div class="stat-value {kmod_class_value}">{kmod_status}</div>
      </div>
    </div>
    <form method="POST" action="/run" id="run-form">
      <div class="card">
        <div class="card-header">
          <div class="card-title">Script</div>
        </div>
        <div class="card-body">
          <div class="editor-wrap">
            <textarea class="editor" name="code" id="code"
                      placeholder="print(&quot;hello&quot;)&#10;print(1 + 2 + 3)"
                      spellcheck="false" autofocus>{code}</textarea>
          </div>
          <div class="editor-toolbar">
            <button type="submit" class="btn btn-primary" id="run-btn">Run script</button>
            <button type="button" class="btn btn-default" onclick="clearEditor()">Clear</button>
            <div class="spacer"></div>
            <span style="color:var(--text-muted);font-size:12px">
              Ctrl+Enter to run
            </span>
          </div>
        </div>
      </div>
    </form>
    <div class="card">
      <div class="card-header">
        <div class="card-title">Output</div>
      </div>
      <pre class="output {css_class}" id="output">{output}</pre>
      {meta}
    </div>
  </main>
</div>
<script>
function insertFn(el) {{
  const fn = el.getAttribute("data-fn");
  const ta = document.getElementById("code");
  const start = ta.selectionStart;
  const before = ta.value.slice(0, start);
  const after = ta.value.slice(ta.selectionEnd);
  ta.value = before + fn + after;
  const pos = start + fn.length;
  ta.setSelectionRange(pos, pos);
  ta.focus();
}}
function clearEditor() {{
  document.getElementById("code").value = "";
  document.getElementById("code").focus();
}}
function filterRef(q) {{
  q = q.toLowerCase();
  document.querySelectorAll(".ref-group").forEach(group => {{
    let any = false;
    group.querySelectorAll(".fn").forEach(fn => {{
      const text = fn.textContent.toLowerCase();
      const show = !q || text.includes(q);
      fn.style.display = show ? "" : "none";
      if (show) any = true;
    }});
    group.style.display = any ? "" : "none";
    if (q && any) group.open = true;
  }});
}}
document.getElementById("code").addEventListener("keydown", function(e) {{
  if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {{
    e.preventDefault();
    document.getElementById("run-form").submit();
  }}
  if (e.key === "Tab") {{
    e.preventDefault();
    const s = this.selectionStart;
    const en = this.selectionEnd;
    this.value = this.value.slice(0, s) + "    " + this.value.slice(en);
    this.setSelectionRange(s + 4, s + 4);
  }}
}});
document.getElementById("run-form").addEventListener("submit", function() {{
  const btn = document.getElementById("run-btn");
  btn.disabled = true;
  btn.textContent = "Running\\u2026";
}});
</script>
</body>
</html>"""


# ── agent management ────────────────────────────────────────────────────
_agent_proc = None

def agent_alive() -> bool:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1.5)
        r = s.connect_ex((AGENT_HOST, AGENT_PORT))
        s.close()
        return r == 0
    except Exception:
        return False


def wait_for_agent(timeout: float = 15.0) -> bool:
    import time
    deadline = time.time() + timeout
    while time.time() < deadline:
        if agent_alive():
            return True
        time.sleep(0.5)
    return False


def start_agent() -> bool:
    global _agent_proc
    if not os.path.isfile(JOCKEY_EXE):
        print(f"[win-srv] ERROR: {JOCKEY_EXE} not found")
        return False
    print(f"[win-srv] starting: {JOCKEY_EXE} --serve {AGENT_PORT}")
    try:
        _agent_proc = subprocess.Popen(
            [JOCKEY_EXE, "--serve", str(AGENT_PORT)],
            cwd=os.path.dirname(JOCKEY_EXE),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return True
    except Exception as e:
        print(f"[win-srv] failed: {e}")
        return False


def _stream_agent_output():
    if not _agent_proc:
        return
    def _reader():
        try:
            for line in _agent_proc.stdout:
                print(f"[agent] {line.decode(errors='replace').rstrip()}")
        except Exception:
            pass
    import threading
    t = threading.Thread(target=_reader, daemon=True)
    t.start()


def kmod_loaded():
    """Non-blocking check: try SCM first, then device open."""
    if platform.system() == "Windows":
        try:
            import ctypes
            scm = ctypes.windll.advapi32.OpenSCManagerW(None, None, 0x0001)
            if scm:
                svc = ctypes.windll.advapi32.OpenServiceW(scm, "jockey", 0x0004)
                if svc:
                    ctypes.windll.advapi32.CloseServiceHandle(svc)
                    ctypes.windll.advapi32.CloseServiceHandle(scm)
                    return True
                ctypes.windll.advapi32.CloseServiceHandle(scm)
        except Exception:
            pass
        try:
            with open(r"\\.\Jockey", "r", buffering=0):
                return True
        except Exception:
            return False
    # Linux fallback
    try:
        with open("/proc/modules") as f:
            return "rootkit" in f.read()
    except Exception:
        return False


# ── compile + send (same protocol as server.py) ─────────────────────────
def compile_jockey(source):
    if LANG_DIR not in sys.path:
        sys.path.insert(0, LANG_DIR)
    for mod in ("lexer", "parser", "ast_nodes", "emitter", "serializer"):
        sys.modules.pop(mod, None)
    from lexer import Lexer
    from parser import Parser
    from emitter import Emitter
    from serializer import serialize_program
    tokens = Lexer(source).tokenize()
    ast = Parser(tokens).parse_program()
    em = Emitter()
    prog = em.compile(ast)
    return serialize_program(prog)


def send_to_agent(bytecode):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(30)
        s.connect((AGENT_HOST, AGENT_PORT))
        s.sendall(struct.pack(">I", len(bytecode)))
        s.sendall(bytecode)
        hdr = b""
        while len(hdr) < 8:
            chunk = s.recv(8 - len(hdr))
            if not chunk:
                return -1, "incomplete header from agent"
            hdr += chunk
        rc = struct.unpack(">I", hdr[:4])[0]
        n  = struct.unpack(">I", hdr[4:8])[0]
        out = b""
        while len(out) < n:
            chunk = s.recv(n - len(out))
            if not chunk:
                break
            out += chunk
        s.close()
        return rc, out.decode("utf-8", errors="replace")
    except ConnectionRefusedError:
        return -1, f"cannot connect to agent at {AGENT_HOST}:{AGENT_PORT}"
    except Exception as e:
        return -1, f"{type(e).__name__}: {e}"


def total_functions():
    return sum(len(fns) for _, fns in REFERENCE)


# ── HTTP handler ────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def _page(self, code="", output="", css_class="info", meta=""):
        alive = agent_alive()
        kmod  = kmod_loaded()
        agent_status = "connected" if alive else "disconnected"
        kmod_status  = "loaded"    if kmod  else "not loaded"
        html = HTML_PAGE.format(
            code=code, output=output, css_class=css_class, meta=meta,
            reference=render_reference(), fn_total=total_functions(),
            target=f"{AGENT_HOST}:{AGENT_PORT}",
            agent_status=agent_status, kmod_status=kmod_status,
            agent_class="on" if alive else "off",
            kmod_class="on" if kmod else "",
            agent_class_value="ok" if alive else "err",
            kmod_class_value="ok" if kmod else "",
        )
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(html.encode())))
        self.end_headers()
        self.wfile.write(html.encode())

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._page()
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/run":
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n).decode("utf-8")
        code = parse_qs(body).get("code", [""])[0]
        if not code.strip():
            self._page(code, "no code provided", "info")
            return
        try:
            bytecode = compile_jockey(code)
        except Exception as e:
            self._page(code, f"compile error: {e}", "err")
            return
        rc, output = send_to_agent(bytecode)
        if rc == 0:
            css = "ok"
            meta = '<div class="output-meta"><span class="k">status</span> <span class="v">ok</span></div>'
        else:
            css = "err"
            meta = f'<div class="output-meta"><span class="k">status</span> <span class="v">error (rc={rc})</span></div>'
        self._page(code, output or "(no output)", css, meta)

    def log_message(self, fmt, *args):
        pass


# ── main ────────────────────────────────────────────────────────────────
def main():
    import time

    print("=" * 52)
    print("  Jockey Windows Server")
    print(f"  Agent  : {AGENT_HOST}:{AGENT_PORT}")
    print(f"  Console: http://0.0.0.0:{WEB_PORT}")
    print(f"  Binary : {JOCKEY_EXE}")
    print("=" * 52)

    # Start agent if not already running
    if not agent_alive():
        if not start_agent():
            print("[win-srv] cannot start agent — build jockey.exe first")
            sys.exit(1)
        _stream_agent_output()
        print(f"[win-srv] waiting for agent on port {AGENT_PORT}...")
        if not wait_for_agent(timeout=20.0):
            print("[win-srv] agent did not respond in time")
            if _agent_proc:
                _agent_proc.kill()
            sys.exit(1)
        print(f"[win-srv] agent up on port {AGENT_PORT}")
    else:
        print(f"[win-srv] agent already listening on {AGENT_HOST}:{AGENT_PORT}")

    kmod = kmod_loaded()
    print(f"[win-srv] kernel module: {'loaded' if kmod else 'not loaded'}")
    print(f"[win-srv] console at http://localhost:{WEB_PORT}")
    print("[win-srv] Ctrl+C to stop")
    print()

    # Monitor agent in background — restart if it dies
    def _monitor():
        global _agent_proc
        while True:
            if _agent_proc and _agent_proc.poll() is not None:
                print("[win-srv] agent exited, restarting in 3s...")
                time.sleep(3)
                start_agent()
                _stream_agent_output()
                wait_for_agent(timeout=15.0)
            time.sleep(2)

    import threading
    threading.Thread(target=_monitor, daemon=True).start()

    HTTPServer(("0.0.0.0", WEB_PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
