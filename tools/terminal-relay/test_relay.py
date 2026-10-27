"""Tests for relay.py: event parsing and the HTTP API with the fake session."""

import json
import os
import sys
import threading
import time
import unittest
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import relay  # noqa: E402


class EventTest(unittest.TestCase):
    def test_stream_events_become_lines_and_a_permission(self):
        s = relay.Session("pc", ".", "claude")
        s.handle_event({"type": "system", "subtype": "init", "session_id": "abc"})
        s.handle_event({"type": "assistant", "message": {"content": [
            {"type": "text", "text": "Running the tests."},
            {"type": "tool_use", "name": "Bash", "input": {"command": "pytest -q"}}]}})
        s.handle_event({"type": "user", "message": {"content": [
            {"type": "tool_result", "content": [{"type": "text", "text": "3 passed\nmore"}]}]}})
        s.handle_event({"type": "result", "session_id": "abc", "permission_denials": [
            {"tool_name": "Bash", "tool_input": {"command": "rm -rf build"}}]})
        kinds = [(l["kind"], l["text"]) for l in s.lines[1:]]
        self.assertEqual(kinds, [("assistant", "Running the tests."), ("tool", "Bash: pytest -q"),
                                 ("tool", "3 passed ...")])
        self.assertEqual(s.session_id, "abc")
        self.assertEqual(s.permission["rule"], "Bash(rm -rf build)")


class HttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = relay.Session("pc", ".", "claude", fake=True)
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), relay.make_handler(cls.session, "k"))
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def call(self, path, body=None, key="k"):
        req = urllib.request.Request(self.base + path, data=json.dumps(body).encode() if body is not None else None,
                                     headers={"Authorization": "Bearer " + key})
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def wait_idle(self):
        for _ in range(50):
            if not self.session.busy:
                return
            time.sleep(0.05)

    def test_prompt_permission_approve(self):
        self.assertEqual(self.call("/api/v1/terminal/session", key="wrong")[0], 401)
        self.assertEqual(self.call("/api/v1/terminal/prompt", {"text": "run the tests"})[0], 200)
        self.wait_idle()
        _, snap = self.call("/api/v1/terminal/session?after=0")
        self.assertEqual(snap["permission"]["tool"], "Bash")
        last = snap["lines"][-1]["seq"]
        self.assertEqual(self.call("/api/v1/terminal/approval", {"id": snap["permission"]["id"], "approve": True})[0],
                         200)
        self.wait_idle()
        _, after = self.call("/api/v1/terminal/session?after=%d" % last)
        self.assertIsNone(after["permission"])
        self.assertIn("All tests pass.", [l["text"] for l in after["lines"]])


if __name__ == "__main__":
    unittest.main()
