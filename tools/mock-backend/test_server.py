"""Tests for the FOs mock backend: python tools/mock-backend/test_server.py"""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server  # noqa: E402


class MockBackendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def call(self, path, body=None, headers=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def test_every_sync_row_has_data(self):
        for path, key in [("/api/v1/health", "ok"), ("/api/v1/budgeteer/categories", "expense"),
                          ("/api/v1/budgeteer/month", "totals"), ("/api/v1/calendar/agenda?days=7", "events"),
                          ("/api/v1/readlater/articles", "articles"), ("/api/v1/flashcards/decks", "decks"),
                          ("/api/v1/puzzles/daily", "answer"), ("/api/v1/calculator/rates", "rates"),
                          ("/api/v1/wallet/codes", "codes"), ("/api/v1/lockscreen/weather", "temp"),
                          ("/api/v1/terminal/session", "lines")]:
            status, body = self.call(path)
            self.assertEqual(status, 200, path)
            self.assertIn(key, body, path)

    def test_categories_match_spec(self):
        _, body = self.call("/api/v1/budgeteer/categories")
        self.assertEqual([c["name"] for c in body["income"]], ["Stipendio", "Introito2"])
        names = [c["name"] for c in body["expense"]]
        self.assertEqual(len(names), 11)
        self.assertTrue(next(c for c in body["expense"] if c["name"] == "NoCat")["excludeFromBalance"])

    def test_transaction_is_stored_once(self):
        tx = {"clientId": "test-1", "type": "expense", "amount": 999, "category": "svago",
              "description": "Test", "date": "2026-10-26"}
        status, body = self.call("/api/v1/budgeteer/transactions", tx)
        self.assertEqual(status, 201)
        self.assertFalse(body["duplicate"])
        status, body = self.call("/api/v1/budgeteer/transactions", tx)
        self.assertEqual(status, 200)
        self.assertTrue(body["duplicate"])
        _, month = self.call("/api/v1/budgeteer/month?month=2026-10")
        self.assertEqual(sum(1 for t in month["transactions"] if t["description"] == "Test" and t["amount"] == 999), 1)

    def test_nocat_is_out_of_balance(self):
        _, month = self.call("/api/v1/budgeteer/month?month=2026-10")
        nocat = sum(t["amount"] for t in month["transactions"] if t["category"] == "nocat")
        expense = sum(t["amount"] for t in month["transactions"] if t["type"] == "expense")
        self.assertEqual(month["totals"]["expense"], expense - nocat)

    def test_read_status_and_terminal(self):
        status, _ = self.call("/api/v1/readlater/articles/a2/read", {})
        self.assertEqual(status, 200)
        _, arts = self.call("/api/v1/readlater/articles")
        self.assertTrue(next(a for a in arts["articles"] if a["id"] == "a2")["read"])
        _, session = self.call("/api/v1/terminal/session")
        pid = session["permission"]["id"]
        status, _ = self.call("/api/v1/terminal/approval", {"id": pid, "approve": True})
        self.assertEqual(status, 200)
        self.call("/api/v1/terminal/prompt", {"text": "explain"})
        _, after = self.call("/api/v1/terminal/session?after=%d" % session["lines"][-1]["seq"])
        self.assertTrue(any(l["kind"] == "assistant" for l in after["lines"]))

    def test_sheet_answers_through_a_redirect(self):
        # urllib follows the 302 like the device does.
        cats = self.call("/gsheet/exec?action=categories&key=sheetkey")[1]
        self.assertIn("nocat", [c["id"] for c in cats["expense"]])
        self.assertEqual(self.call("/gsheet/exec?action=categories&key=wrong")[1], {"error": "unauthorized"})
        body = {"key": "sheetkey", "action": "add", "clientId": "sheet-test-1", "type": "expense", "amount": 990,
                "category": "spesa", "description": "Pane", "date": "2026-10-26"}
        first = self.call("/gsheet/exec", body)[1]
        again = self.call("/gsheet/exec", body)[1]
        self.assertEqual((first["duplicate"], again["duplicate"]), (False, True))

    def test_direct_sources(self):
        self.assertIn("results", self.call("/direct/geocoding-api.open-meteo.com/v1/search?name=milano")[1])
        self.assertIn("current", self.call("/direct/api.open-meteo.com/v1/forecast?latitude=1&longitude=2")[1])
        self.assertIn("USD", self.call("/direct/api.frankfurter.app/latest?from=EUR")[1]["rates"])
        with urllib.request.urlopen(self.base + "/ics/calendar.ics") as r:
            text = r.read().decode("utf-8")
        self.assertIn("BEGIN:VEVENT", text)
        self.assertIn("RRULE:FREQ=WEEKLY", text)

    def test_key_required_when_set(self):
        server.Handler.key = "k"
        try:
            self.assertEqual(self.call("/api/v1/health")[0], 401)
            self.assertEqual(self.call("/api/v1/health", headers={"Authorization": "Bearer k"})[0], 200)
        finally:
            server.Handler.key = None


if __name__ == "__main__":
    unittest.main()
