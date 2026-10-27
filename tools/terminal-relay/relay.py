#!/usr/bin/env python3
"""FOs Terminal relay: drives Claude Code on this computer for the Sticky.

Run it in the project folder you want Claude to work in:

    python tools/terminal-relay/relay.py --cwd C:/path/to/project
    python tools/terminal-relay/relay.py --cwd . --host 0.0.0.0 --port 8790

It prints the address and key to enter on the device (Settings > Accounts >
Terminal relay). Standard library only.

API (the same as the backend's terminal rows, so the device app is one client):
    GET  /api/v1/terminal/session?after=<seq>  -> {"session", "connected", "lines": [...], "permission"}
    POST /api/v1/terminal/prompt   {"text"}
    POST /api/v1/terminal/approval {"id", "approve"}

Each prompt runs `claude -p <text> --output-format stream-json --verbose`,
resuming the same session, and turns the streamed events into transcript
lines. In print mode Claude Code cannot ask for permission, so a tool that
needs it is denied and listed in the result event's "permission_denials".
The relay shows the last denial as a permission request on the device;
Approve re-runs with that tool allowed (--allowedTools) and asks Claude to
continue. Your settings' allow rules apply as usual.

    python tools/terminal-relay/relay.py --fake   # scripted session, no Claude (testing)
"""

import argparse
import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

MAX_LINES = 500
MAX_TEXT = 4000  # per line; long tool output is cut


class Session:
    """The transcript the device polls, and the Claude Code process behind it."""

    def __init__(self, name, cwd, claude, fake=False):
        self.lock = threading.RLock()
        self.name = name
        self.cwd = cwd
        self.claude = claude
        self.fake = fake
        self.seq = 0
        self.lines = []
        self.permission = None
        self.session_id = None
        self.allowed = set()
        self.busy = False
        self.add("system", "Relay ready in %s%s" % (cwd, " (fake session)" if fake else ""))

    def add(self, kind, text):
        text = (text or "").strip()
        if not text:
            return
        if len(text) > MAX_TEXT:
            text = text[:MAX_TEXT] + " ..."
        with self.lock:
            self.seq += 1
            self.lines.append({"seq": self.seq, "kind": kind, "text": text})
            del self.lines[:-MAX_LINES]

    def snapshot(self, after):
        with self.lock:
            return {"session": self.name, "connected": True, "busy": self.busy,
                    "lines": [l for l in self.lines if l["seq"] > after], "permission": self.permission}

    # --- prompts ---------------------------------------------------------------

    def prompt(self, text):
        with self.lock:
            if self.busy:
                return False
            self.busy = True
            self.permission = None
        self.add("user", text)
        threading.Thread(target=self._run, args=(text,), daemon=True).start()
        return True

    def approve(self, pid, ok):
        with self.lock:
            if not self.permission or self.permission["id"] != pid or self.busy:
                return False
            perm, self.permission = self.permission, None
        if not ok:
            self.add("system", "Denied: %s" % perm["detail"])
            return True
        self.allowed.add(perm["rule"])
        self.add("system", "Allowed: %s" % perm["rule"])
        return self.prompt("Permission granted for %s. Continue." % perm["tool"])

    def _run(self, text):
        try:
            if self.fake:
                self._fake(text)
            else:
                self._claude(text)
        except Exception as e:  # report, never kill the relay
            self.add("system", "Error: %s" % e)
        finally:
            with self.lock:
                self.busy = False

    def _claude(self, text):
        cmd = [self.claude, "-p", text, "--output-format", "stream-json", "--verbose"]
        if self.session_id:
            cmd += ["--resume", self.session_id]
        if self.allowed:
            cmd += ["--allowedTools"] + sorted(self.allowed)
        proc = subprocess.Popen(cmd, cwd=self.cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                encoding="utf-8", errors="replace")
        for raw in proc.stdout:
            raw = raw.strip()
            if not raw:
                continue
            try:
                self.handle_event(json.loads(raw))
            except ValueError:
                self.add("tool", raw)
        proc.wait()
        err = proc.stderr.read().strip()
        if proc.returncode != 0 and err:
            self.add("system", err.splitlines()[-1])

    # --- stream-json events -------------------------------------------------------

    def handle_event(self, ev):
        kind = ev.get("type")
        if ev.get("session_id"):
            self.session_id = ev["session_id"]
        if kind == "assistant":
            for block in ev.get("message", {}).get("content", []):
                if block.get("type") == "text":
                    self.add("assistant", block.get("text", ""))
                elif block.get("type") == "tool_use":
                    self.add("tool", "%s: %s" % (block.get("name", "tool"), describe_input(block.get("input"))))
        elif kind == "user":
            for block in ev.get("message", {}).get("content", []):
                if isinstance(block, dict) and block.get("type") == "tool_result":
                    content = block.get("content")
                    if isinstance(content, list):
                        content = " ".join(c.get("text", "") for c in content if isinstance(c, dict))
                    first = str(content or "").strip().splitlines()
                    if first:
                        self.add("tool", first[0] + (" ..." if len(first) > 1 else ""))
        elif kind == "result":
            denials = ev.get("permission_denials") or []
            if denials:
                d = denials[-1]
                tool = d.get("tool_name", "tool")
                detail = describe_input(d.get("tool_input"))
                rule = tool
                if tool == "Bash" and isinstance(d.get("tool_input"), dict) and d["tool_input"].get("command"):
                    rule = "Bash(%s)" % d["tool_input"]["command"]
                with self.lock:
                    self.permission = {"id": "p%d" % (self.seq + 1), "tool": tool, "detail": detail, "rule": rule}
            if ev.get("is_error") and ev.get("result"):
                self.add("system", str(ev["result"]))

    # --- fake session (tests) -------------------------------------------------------

    def _fake(self, text):
        t = text.strip().lower()
        if t.startswith("permission granted"):
            self.add("tool", "Bash: ctest --output-on-failure")
            self.add("tool", "100% tests passed, 0 tests failed out of 12")
            self.add("assistant", "All tests pass.")
            return
        if "test" in t:
            self.add("assistant", "Running the test suite.")
            self.add("tool", "Bash: ctest --output-on-failure")
            with self.lock:
                self.permission = {"id": "p%d" % (self.seq + 1), "tool": "Bash",
                                   "detail": "ctest --output-on-failure", "rule": "Bash(ctest --output-on-failure)"}
            return
        self.add("assistant", "Okay: %s" % text)


def describe_input(value):
    if isinstance(value, dict):
        for key in ("command", "file_path", "path", "pattern", "url", "description"):
            if value.get(key):
                return str(value[key])
        return json.dumps(value, ensure_ascii=False)[:200]
    return str(value or "")


def make_handler(session, key):
    class Handler(BaseHTTPRequestHandler):
        server_version = "FOsRelay/1.0"

        def log_message(self, fmt, *args):
            pass

        def send_json(self, code, body):
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def authorized(self):
            return not key or self.headers.get("Authorization", "") == "Bearer %s" % key

        def do_GET(self):
            url = urlparse(self.path)
            if not self.authorized():
                return self.send_json(401, {"error": "unauthorized"})
            if url.path.rstrip("/") == "/api/v1/terminal/session":
                q = {k: v[-1] for k, v in parse_qs(url.query).items()}
                try:
                    after = int(q.get("after", 0))
                except ValueError:
                    after = 0
                return self.send_json(200, session.snapshot(after))
            if url.path.rstrip("/") == "/api/v1/health":
                return self.send_json(200, {"ok": True, "server": "fos-terminal-relay"})
            self.send_json(404, {"error": "not found"})

        def do_POST(self):
            if not self.authorized():
                return self.send_json(401, {"error": "unauthorized"})
            length = int(self.headers.get("Content-Length") or 0)
            try:
                body = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
            except (ValueError, UnicodeDecodeError):
                return self.send_json(400, {"error": "invalid json"})
            path = urlparse(self.path).path.rstrip("/")
            if path == "/api/v1/terminal/prompt":
                ok = session.prompt(str(body.get("text", ""))[:2000])
                return self.send_json(200 if ok else 409, {"ok": ok})
            if path == "/api/v1/terminal/approval":
                ok = session.approve(body.get("id"), bool(body.get("approve")))
                return self.send_json(200 if ok else 409, {"ok": ok})
            self.send_json(404, {"error": "not found"})

    return Handler


def lan_address():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cwd", default=os.getcwd(), help="folder Claude Code works in")
    parser.add_argument("--host", default="0.0.0.0", help="0.0.0.0 = reachable from the device on your network")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--key", default=os.environ.get("FOS_RELAY_KEY"), help="default: a new random key")
    parser.add_argument("--name", default=socket.gethostname(), help="session name shown on the device")
    parser.add_argument("--claude", default=shutil.which("claude") or "claude", help="the claude executable")
    parser.add_argument("--fake", action="store_true", help="scripted session without Claude (testing)")
    parser.add_argument("--no-key", action="store_true", help="accept requests without a key (local tests)")
    args = parser.parse_args()

    key = None if args.no_key else (args.key or secrets.token_urlsafe(9))
    session = Session(args.name, os.path.abspath(args.cwd), args.claude, fake=args.fake)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(session, key))
    host = lan_address() if args.host == "0.0.0.0" else args.host
    print("FOs terminal relay for %s" % session.cwd)
    print("  Relay address: http://%s:%d" % (host, args.port))
    print("  Relay key:     %s" % (key or "(none)"))
    print("Enter both on the Sticky: Settings > Accounts > Terminal relay. Ctrl+C stops.")
    sys.stdout.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
