#!/usr/bin/env python3
"""FOs mock backend (Python standard library only).

Serves sample data for every row of the spec's "Sync and configuration" table
and accepts what the device sends. State lives in memory; pass --state FILE to
keep it across restarts. Open http://127.0.0.1:8787/ for a status page.

    python tools/mock-backend/server.py                 # 127.0.0.1:8787
    python tools/mock-backend/server.py --host 0.0.0.0  # reachable from the device
    python tools/mock-backend/server.py --key secret    # require "Authorization: Bearer secret"

All endpoints are JSON under /api/v1. Amounts are integer cents; dates are
ISO 8601 in local time. Lichess is not part of the backend; a small mock of
the endpoints FOs uses lives under /lichess/ for testing (see README.md).
"""

import argparse
import datetime as dt
import html
import json
import os
import random
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LOCK = threading.RLock()
LOG = []  # recent requests the device sent, newest last


def today():
    return dt.date.today()


def iso(d):
    return d.isoformat()


# --- sample data ------------------------------------------------------------

INCOME = [("stipendio", "Stipendio"), ("introito2", "Introito2")]
EXPENSE = [
    ("equitalia", "Equitalia"), ("bollette", "Bollette"), ("trasporti", "Trasporti"), ("spesa", "Spesa"),
    ("svago", "Svago"), ("ristoranti", "Ristoranti"), ("risparmio", "Risparmio"), ("abbonamenti", "Abbonamenti"),
    ("salute", "Salute"), ("cura", "Cura"), ("nocat", "NoCat"),
]
BUDGETS = {"bollette": 18000, "trasporti": 9000, "spesa": 45000, "svago": 15000, "ristoranti": 20000,
           "risparmio": 30000, "abbonamenti": 6000, "salute": 8000, "cura": 5000, "equitalia": 10000}

SAMPLE_EXPENSES = [
    ("spesa", "Esselunga", 4235), ("ristoranti", "Pizzeria da Gino", 2800), ("trasporti", "ATM monthly pass", 3900),
    ("bollette", "Enel luce", 6420), ("abbonamenti", "Spotify", 1199), ("svago", "Cinema", 1800),
    ("spesa", "Mercato", 1650), ("salute", "Farmacia", 1290), ("cura", "Barbiere", 2000),
    ("ristoranti", "Sushi", 3550), ("spesa", "Carrefour", 2875), ("nocat", "Prelievo", 5000),
    ("risparmio", "Conto deposito", 30000), ("trasporti", "Benzina", 6000), ("bollette", "Fastweb", 2995),
]


def seed_transactions():
    rng = random.Random(7)
    txs = []
    first = today().replace(day=1)
    prev = (first - dt.timedelta(days=1)).replace(day=1)
    for month_start in (prev, first):
        last_day = today().day if month_start == first else 28
        txs.append({"id": str(uuid.UUID(int=rng.getrandbits(128))), "date": iso(month_start.replace(day=1)),
                    "type": "income", "category": "stipendio", "amount": 245000, "description": "Stipendio"})
        for i, (cat, desc, amount) in enumerate(SAMPLE_EXPENSES):
            day = min(last_day, 1 + (i * 2) % 28)
            txs.append({"id": str(uuid.UUID(int=rng.getrandbits(128))), "date": iso(month_start.replace(day=day)),
                        "type": "expense", "category": cat, "amount": amount, "description": desc})
    return txs


def agenda(days):
    base = today()
    items = [
        (0, "09:30", "10:00", "Stand-up", "Teams"), (0, "13:00", "14:00", "Lunch with Marta", "Bar Magenta"),
        (0, "18:30", "19:30", "Gym", ""), (1, "08:00", "08:30", "Dentist", "Via Torino 12"),
        (1, "15:00", "16:00", "Design review", "Room 3"), (2, None, None, "Mum's birthday", ""),
        (3, "20:30", "23:00", "Chess club", "Circolo Scacchi"), (5, "10:00", "12:00", "Run in the park", "Sempione"),
        (6, "19:00", "21:00", "Dinner at Luca's", ""),
    ]
    out = []
    for offset, start, end, title, where in items:
        if offset >= days:
            continue
        d = base + dt.timedelta(days=offset)
        event = {"id": "ev-%d-%s" % (offset, title.lower().replace(" ", "-")), "title": title, "location": where}
        if start:
            event.update({"start": "%sT%s" % (iso(d), start), "end": "%sT%s" % (iso(d), end), "allDay": False})
        else:
            event.update({"start": iso(d), "end": iso(d), "allDay": True})
        out.append(event)
    return out


ARTICLES = [
    {"id": "a1", "title": "Why e-paper is still the best screen for reading", "source": "Tech Notes",
     "url": "https://example.com/e-paper", "words": 420,
     "text": "E-paper reflects light instead of emitting it. That single fact explains most of why it is easy on "
             "the eyes.\n\nAn electrophoretic display moves charged pigment particles to the surface of each "
             "pixel. Once they are there, they stay put without power, which is why a reader can show a page for "
             "weeks on one charge.\n\nThe trade-off is speed. A full refresh takes hundreds of milliseconds, and "
             "partial refreshes leave faint ghosts that a periodic full refresh cleans up.\n\nFor text, none of "
             "that matters much. Pages change slowly, contrast is high, and the screen looks like paper in "
             "direct sunlight."},
    {"id": "a2", "title": "The case for a pocket notebook", "source": "Slow Living",
     "url": "https://example.com/notebook", "words": 310,
     "text": "Writing things down is the oldest productivity system there is.\n\nA notebook never needs charging, "
             "never sends a notification and never asks you to update. It does one thing.\n\nThe best "
             "systems are the ones you actually use. Keep it small enough to carry everywhere."},
    {"id": "a3", "title": "Learning chess as an adult", "source": "Board Game Weekly",
     "url": "https://example.com/chess", "words": 380,
     "text": "Adults learn chess differently from children. They want reasons, not rules.\n\nStart with "
             "endgames: king and pawn against king teaches opposition, the most important idea in chess.\n\n"
             "Then play slow games. Correspondence chess gives you a day per move, which is plenty of time to "
             "look at every check, capture and threat."},
]

DECKS = [
    {"id": "italian-basics", "name": "Italian basics", "cards": [
        {"id": "c1", "front": "the apple", "back": "la mela"}, {"id": "c2", "front": "the house", "back": "la casa"},
        {"id": "c3", "front": "to eat", "back": "mangiare"}, {"id": "c4", "front": "thank you", "back": "grazie"},
        {"id": "c5", "front": "good evening", "back": "buonasera"}, {"id": "c6", "front": "the train", "back": "il treno"},
        {"id": "c7", "front": "to speak", "back": "parlare"}, {"id": "c8", "front": "the key", "back": "la chiave"}]},
    {"id": "capitals", "name": "World capitals", "cards": [
        {"id": "k1", "front": "Portugal", "back": "Lisbon"}, {"id": "k2", "front": "Canada", "back": "Ottawa"},
        {"id": "k3", "front": "Australia", "back": "Canberra"}, {"id": "k4", "front": "Kenya", "back": "Nairobi"},
        {"id": "k5", "front": "Norway", "back": "Oslo"}, {"id": "k6", "front": "Chile", "back": "Santiago"}]},
]

DAILY_WORDS = ["PIANO", "LEMON", "CRANE", "STORM", "GHOST", "PLANT", "BRICK", "MOUSE", "FLAME", "ROBIN"]

RATES = {"USD": 1.0842, "GBP": 0.8391, "CHF": 0.9412, "JPY": 162.31, "SEK": 11.402, "PLN": 4.317}

WALLET = [
    {"id": "w1", "label": "Esselunga Fidaty", "kind": "barcode", "format": "ean13", "payload": "2501234567898"},
    {"id": "w2", "label": "Gym membership", "kind": "qr", "format": "qr", "payload": "FOS-GYM-000417"},
    {"id": "w3", "label": "Train MI-TO 18:05", "kind": "qr", "format": "qr",
     "payload": "TRENITALIA|PNR=QX7K2P|COACH=7|SEAT=42A|DATE=%s" % iso(today())},
    {"id": "w4", "label": "Library card", "kind": "barcode", "format": "code128", "payload": "LIB-88213-MI"},
]


def weather():
    return {"location": "Milano", "temp": 14.5, "condition": "Partly cloudy", "high": 17, "low": 9,
            "humidity": 72, "updated": dt.datetime.now().replace(microsecond=0).isoformat()}


class Terminal:
    """A scripted Claude Code session: replies to prompts, asks permission for tools."""

    def __init__(self):
        self.seq = 0
        self.lines = []
        self.permission = None
        self.add("system", "Connected to claude on laptop (~/code/fos)")
        self.add("user", "add a unit test for the keypad decimal handling")
        self.add("assistant", "I'll add tests for Keypad::press and Keypad::scaled in test/fos_keypad.")
        self.add("tool", "Read src/fos/ui/Keyboard.cpp (212 lines)")
        self.add("assistant", "The keypad keeps '.' internally and caps decimals at 2. Writing the test now.")
        self.permission = {"id": "p1", "tool": "Bash", "detail": "cmake --build build/test && ctest"}

    def add(self, kind, text):
        self.seq += 1
        self.lines.append({"seq": self.seq, "kind": kind, "text": text})

    def prompt(self, text):
        self.add("user", text)
        canned = {
            "continue": "Continuing: the test file compiles; running it next.",
            "run the tests": "Running the test suite.",
            "explain": "Keypad::scaled() turns the typed text into an integer number of cents, so no floating "
                       "point is involved in money amounts.",
        }
        reply = canned.get(text.strip().lower(), "Okay. Working on: %s" % text)
        self.add("assistant", reply)
        if "test" in text.lower() and not self.permission:
            self.permission = {"id": "p%d" % self.seq, "tool": "Bash", "detail": "ctest --output-on-failure"}

    def approve(self, pid, approve):
        if not self.permission or self.permission["id"] != pid:
            return False
        tool = self.permission["detail"]
        self.permission = None
        if approve:
            self.add("tool", "$ %s" % tool)
            self.add("tool", "100% tests passed, 0 tests failed out of 12")
            self.add("assistant", "All tests pass.")
        else:
            self.add("system", "Denied: %s" % tool)
            self.add("assistant", "Understood, I won't run that.")
        return True


class State:
    def __init__(self):
        self.transactions = seed_transactions()
        self.client_ids = set()
        self.read = set()
        self.terminal = Terminal()
        self.lichess_games = {
            "g1": {"id": "g1", "opponent": "marta_k", "color": "white", "speed": "correspondence",
                   "moves": ["e2e4", "c7c5", "g1f3", "d7d6"], "isMyTurn": True},
            "g2": {"id": "g2", "opponent": "luca88", "color": "black", "speed": "classical",
                   "moves": ["d2d4", "g8f6", "c2c4"], "isMyTurn": True},
        }

    def to_json(self):
        return {"transactions": self.transactions, "client_ids": sorted(self.client_ids), "read": sorted(self.read)}

    def load(self, data):
        self.transactions = data.get("transactions", self.transactions)
        self.client_ids = set(data.get("client_ids", []))
        self.read = set(data.get("read", []))


STATE = State()


def month_view(month):
    try:
        year, mon = (int(x) for x in month.split("-"))
    except ValueError:
        t = today()
        year, mon = t.year, t.month
    prefix = "%04d-%02d" % (year, mon)
    txs = sorted((t for t in STATE.transactions if t["date"].startswith(prefix)), key=lambda t: t["date"],
                 reverse=True)
    income = sum(t["amount"] for t in txs if t["type"] == "income")
    # NoCat stays out of the monthly balance.
    expense = sum(t["amount"] for t in txs if t["type"] == "expense" and t["category"] != "nocat")
    spent = {}
    for t in txs:
        if t["type"] == "expense":
            spent[t["category"]] = spent.get(t["category"], 0) + t["amount"]
    budgets = [{"category": cid, "budget": BUDGETS.get(cid, 0), "spent": spent.get(cid, 0)}
               for cid, _ in EXPENSE if cid in BUDGETS or cid in spent]
    return {"month": prefix, "totals": {"income": income, "expense": expense, "balance": income - expense},
            "budgets": budgets, "transactions": txs}


# --- direct sources (used by the device when there is no backend) -------------

SHEET_KEY = "sheetkey"
SHEET_REPLIES = {}  # echo id -> JSON body (Apps Script answers through a redirect)


def ics_calendar():
    """An iCal feed with the sample agenda, plus a weekly series."""
    def stamp(d, hhmm):
        return "%s%s00" % (d.strftime("%Y%m%d"), "T" + hhmm.replace(":", "")) if hhmm else d.strftime("%Y%m%d")

    lines = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//FOs mock//EN"]
    base = today()
    for i, ev in enumerate(agenda(7)):
        d = dt.date.fromisoformat(ev["start"][:10])
        lines += ["BEGIN:VEVENT", "UID:ics-%d" % i, "SUMMARY:%s (iCal)" % ev["title"]]
        if ev["location"]:
            lines.append("LOCATION:%s" % ev["location"].replace(",", "\\,"))
        if ev["allDay"]:
            lines += ["DTSTART;VALUE=DATE:%s" % stamp(d, None),
                      "DTEND;VALUE=DATE:%s" % stamp(d + dt.timedelta(days=1), None)]
        else:
            lines += ["DTSTART;TZID=Europe/Rome:%s" % stamp(d, ev["start"][11:16]),
                      "DTEND;TZID=Europe/Rome:%s" % stamp(d, ev["end"][11:16])]
        lines.append("END:VEVENT")
    # A weekly series that started months ago, with one week skipped.
    start = base - dt.timedelta(days=70)
    lines += ["BEGIN:VEVENT", "UID:ics-weekly", "SUMMARY:Piano lesson",
              "DTSTART:%s" % stamp(start, "17:00"), "DTEND:%s" % stamp(start, "18:00"),
              "RRULE:FREQ=WEEKLY", "EXDATE:%s" % stamp(start + dt.timedelta(days=7), "17:00"), "END:VEVENT"]
    lines.append("END:VCALENDAR")
    return "\r\n".join(lines) + "\r\n"


def open_meteo_forecast():
    return {"current": {"temperature_2m": 12.3, "relative_humidity_2m": 81, "weather_code": 61},
            "daily": {"temperature_2m_max": [15.1], "temperature_2m_min": [7.4]}}


# --- HTTP -------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "FOsMock/1.0"
    key = None
    state_file = None

    def log_message(self, fmt, *args):
        print("%s  %s" % (self.address_string(), fmt % args))

    def send_json(self, code, body):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return None

    def authorized(self):
        if not self.key:
            return True
        return self.headers.get("Authorization", "") == "Bearer %s" % self.key

    def record(self, what, payload):
        with LOCK:
            LOG.append({"time": dt.datetime.now().strftime("%H:%M:%S"), "what": what, "payload": payload})
            del LOG[:-50]
        print("  <- device sent %s: %s" % (what, json.dumps(payload, ensure_ascii=False)))

    def save_state(self):
        if self.state_file:
            with open(self.state_file, "w", encoding="utf-8") as f:
                json.dump(STATE.to_json(), f, indent=1)

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[-1] for k, v in parse_qs(url.query).items()}
        path = url.path.rstrip("/") or "/"
        if path == "/":
            return self.status_page()
        if path.startswith("/lichess/"):
            return self.lichess_get(path[len("/lichess"):], q)
        if path.startswith("/direct/"):
            return self.direct_get(path[len("/direct/"):], q)
        if path == "/ics/calendar.ics":
            data = ics_calendar().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/calendar; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/gsheet/exec":
            return self.sheet_redirect(self.sheet_get(q))
        if path == "/gsheet/echo":
            with LOCK:
                body = SHEET_REPLIES.pop(q.get("id", ""), {"error": "expired"})
            return self.send_json(200, body)
        if not self.authorized():
            return self.send_json(401, {"error": "unauthorized"})
        with LOCK:
            if path == "/api/v1/health":
                return self.send_json(200, {"ok": True, "server": "fos-mock", "time": dt.datetime.now().isoformat()})
            if path == "/api/v1/budgeteer/categories":
                return self.send_json(200, {
                    "income": [{"id": i, "name": n} for i, n in INCOME],
                    "expense": [{"id": i, "name": n, "excludeFromBalance": i == "nocat"} for i, n in EXPENSE]})
            if path == "/api/v1/budgeteer/month":
                return self.send_json(200, month_view(q.get("month", today().strftime("%Y-%m"))))
            if path == "/api/v1/calendar/agenda":
                return self.send_json(200, {"from": iso(today()), "events": agenda(int(q.get("days", 7)))})
            if path == "/api/v1/readlater/articles":
                arts = [dict(a, saved=iso(today() - dt.timedelta(days=i)), read=a["id"] in STATE.read)
                        for i, a in enumerate(ARTICLES)]
                return self.send_json(200, {"articles": arts})
            if path == "/api/v1/flashcards/decks":
                return self.send_json(200, {"decks": DECKS})
            if path == "/api/v1/puzzles/daily":
                word = DAILY_WORDS[today().toordinal() % len(DAILY_WORDS)]
                return self.send_json(200, {"date": iso(today()), "kind": "word", "length": len(word),
                                            "maxGuesses": 6, "answer": word})
            if path == "/api/v1/calculator/rates":
                return self.send_json(200, {"base": "EUR", "date": iso(today()), "rates": RATES})
            if path == "/api/v1/wallet/codes":
                return self.send_json(200, {"codes": WALLET})
            if path == "/api/v1/lockscreen/weather":
                return self.send_json(200, weather())
            if path == "/api/v1/terminal/session":
                after = int(q.get("after", 0))
                t = STATE.terminal
                return self.send_json(200, {"session": "laptop", "connected": True,
                                            "lines": [l for l in t.lines if l["seq"] > after],
                                            "permission": t.permission})
        self.send_json(404, {"error": "not found", "path": path})

    def do_POST(self):
        url = urlparse(self.path)
        path = url.path.rstrip("/")
        if path.startswith("/lichess/"):
            return self.lichess_post(path[len("/lichess"):])
        if path == "/gsheet/exec":
            return self.sheet_redirect(self.sheet_post(self.body()))
        if not self.authorized():
            return self.send_json(401, {"error": "unauthorized"})
        payload = self.body()
        if payload is None:
            return self.send_json(400, {"error": "invalid json"})
        with LOCK:
            if path == "/api/v1/budgeteer/transactions":
                self.record("transaction", payload)
                missing = [k for k in ("clientId", "type", "amount", "category", "date") if k not in payload]
                if missing:
                    return self.send_json(400, {"error": "missing " + ", ".join(missing)})
                if payload["clientId"] in STATE.client_ids:
                    return self.send_json(200, {"ok": True, "duplicate": True, "clientId": payload["clientId"]})
                STATE.client_ids.add(payload["clientId"])
                tx = {"id": str(uuid.uuid4()), "date": payload["date"], "type": payload["type"],
                      "category": payload["category"], "amount": int(payload["amount"]),
                      "description": payload.get("description", "")}
                STATE.transactions.append(tx)
                self.save_state()
                return self.send_json(201, {"ok": True, "duplicate": False, "id": tx["id"],
                                            "clientId": payload["clientId"]})
            if path.startswith("/api/v1/readlater/articles/") and path.endswith("/read"):
                aid = path.split("/")[-2]
                self.record("read status", {"id": aid})
                STATE.read.add(aid)
                self.save_state()
                return self.send_json(200, {"ok": True})
            if path == "/api/v1/terminal/prompt":
                self.record("terminal prompt", payload)
                STATE.terminal.prompt(str(payload.get("text", "")))
                return self.send_json(200, {"ok": True})
            if path == "/api/v1/terminal/approval":
                self.record("terminal approval", payload)
                ok = STATE.terminal.approve(payload.get("id"), bool(payload.get("approve")))
                return self.send_json(200 if ok else 409, {"ok": ok})
        self.send_json(404, {"error": "not found", "path": path})

    # Google Apps Script web app mock (tools/google-sheets/Budgeteer.gs): same
    # actions and replies, and like Apps Script every answer comes through a
    # 302 to a second URL, so the device's redirect handling is exercised.
    def sheet_redirect(self, body):
        with LOCK:
            rid = uuid.uuid4().hex[:12]
            SHEET_REPLIES[rid] = body
        self.send_response(302)
        self.send_header("Location", "/gsheet/echo?id=%s" % rid)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def sheet_get(self, q):
        if q.get("key") != SHEET_KEY:
            return {"error": "unauthorized"}
        with LOCK:
            action = q.get("action")
            if action == "health":
                return {"ok": True, "server": "fos-sheet-mock"}
            if action == "categories":
                return {"income": [{"id": i, "name": n} for i, n in INCOME],
                        "expense": [{"id": i, "name": n, "excludeFromBalance": i == "nocat"} for i, n in EXPENSE]}
            if action == "month":
                return month_view(q.get("month", today().strftime("%Y-%m")))
        return {"error": "unknown action"}

    def sheet_post(self, payload):
        if payload is None:
            return {"error": "invalid json"}
        if payload.get("key") != SHEET_KEY:
            return {"error": "unauthorized"}
        if payload.get("action") != "add":
            return {"error": "unknown action"}
        missing = [k for k in ("clientId", "type", "amount", "category", "date") if k not in payload]
        if missing:
            return {"error": "missing " + ", ".join(missing)}
        clean = {k: v for k, v in payload.items() if k != "key"}
        self.record("sheet row", clean)
        with LOCK:
            if payload["clientId"] in STATE.client_ids:
                return {"ok": True, "duplicate": True, "clientId": payload["clientId"]}
            STATE.client_ids.add(payload["clientId"])
            STATE.transactions.append({"id": str(uuid.uuid4()), "date": payload["date"], "type": payload["type"],
                                       "category": payload["category"], "amount": int(payload["amount"]),
                                       "description": payload.get("description", "")})
            self.save_state()
        return {"ok": True, "duplicate": False, "clientId": payload["clientId"]}

    # Public APIs the device calls without a backend (FOS_SIM_DIRECT_BASE in
    # the simulator sends https://<host>/<path> here as /direct/<host>/<path>).
    def direct_get(self, rest, q):
        host, _, path = rest.partition("/")
        path = "/" + path
        if host == "geocoding-api.open-meteo.com" and path == "/v1/search":
            name = q.get("name", "")
            if name.lower() in ("nowhere", ""):
                return self.send_json(200, {"generationtime_ms": 0.1})
            return self.send_json(200, {"results": [{"name": name.title(), "latitude": 45.4643,
                                                     "longitude": 9.1895, "country": "Italia"}]})
        if host == "api.open-meteo.com" and path == "/v1/forecast":
            return self.send_json(200, open_meteo_forecast())
        if host == "api.frankfurter.app" and path == "/latest":
            return self.send_json(200, {"amount": 1.0, "base": "EUR", "date": iso(today()),
                                        "rates": {"USD": 1.0901, "GBP": 0.8402, "CHF": 0.9377, "JPY": 163.1}})
        return self.send_json(404, {"error": "not found", "host": host, "path": path})

    # Minimal Lichess-compatible mock (only what FOs uses). Point Settings >
    # Accounts > Lichess server at http://<host>:8787/lichess to use it.
    def lichess_get(self, path, q):
        if path == "/api/account":
            return self.send_json(200, {"id": "fos_player", "username": "fos_player",
                                        "perfs": {"correspondence": {"rating": 1534}, "classical": {"rating": 1488},
                                                  "rapid": {"rating": 1450}}})
        if path == "/api/account/playing":
            games = [{"gameId": g["id"], "opponent": {"username": g["opponent"]}, "color": g["color"],
                      "speed": g["speed"], "isMyTurn": g["isMyTurn"], "moves": " ".join(g["moves"])}
                     for g in STATE.lichess_games.values()]
            return self.send_json(200, {"nowPlaying": games})
        if path == "/api/puzzle/daily":
            return self.send_json(200, {
                "game": {"pgn": "e4 e5 Nf3 Nc6 Bc4 Nd4 Nxe5 Qg5"},
                "puzzle": {"id": "fos01", "rating": 1420, "initialPly": 7,
                           "fen": "r1b1kbnr/pppp1ppp/8/4N1q1/2BnP3/8/PPPP1PPP/RNBQK2R w KQkq - 2 5",
                           "solution": ["e5f7", "g5g2", "h1f1", "g2e4", "c4e2", "d4f3"], "themes": ["fork"]}})
        return self.send_json(404, {"error": "not found"})

    def lichess_post(self, path):
        parts = path.strip("/").split("/")
        # /api/board/game/{id}/move/{uci}
        if len(parts) == 6 and parts[:3] == ["api", "board", "game"] and parts[4] == "move":
            with LOCK:
                game = STATE.lichess_games.get(parts[3])
                if not game:
                    return self.send_json(404, {"error": "no such game"})
                self.record("lichess move", {"game": parts[3], "move": parts[5]})
                game["moves"].append(parts[5])
                game["isMyTurn"] = False
            return self.send_json(200, {"ok": True})
        return self.send_json(404, {"error": "not found"})

    def status_page(self):
        with LOCK:
            rows = "".join("<tr><td>%s</td><td>%s</td><td><code>%s</code></td></tr>" %
                           (html.escape(e["time"]), html.escape(e["what"]),
                            html.escape(json.dumps(e["payload"], ensure_ascii=False)))
                           for e in reversed(LOG))
            count = len(STATE.transactions)
        page = ("<!doctype html><meta charset=utf-8><title>FOs mock backend</title>"
                "<style>body{font-family:system-ui;margin:2em}td{border-top:1px solid #ccc;padding:4px 8px}</style>"
                "<h1>FOs mock backend</h1><p>%d transactions stored. Key required: %s.</p>"
                "<h2>Received from the device</h2><table>%s</table>" %
                (count, "yes" if self.key else "no", rows or "<tr><td>nothing yet</td></tr>"))
        data = page.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    parser = argparse.ArgumentParser(description="FOs mock backend")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--key", default=os.environ.get("FOS_MOCK_KEY"), help="require this bearer key")
    parser.add_argument("--state", help="JSON file to keep state across restarts")
    args = parser.parse_args()

    Handler.key = args.key
    Handler.state_file = args.state
    if args.state and os.path.isfile(args.state):
        with open(args.state, encoding="utf-8") as f:
            STATE.load(json.load(f))
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("FOs mock backend on http://%s:%d/  (Ctrl+C to stop)" % (args.host, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
