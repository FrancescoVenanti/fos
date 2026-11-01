#!/usr/bin/env python3
"""Test-only stand-in for the internet services FOs calls directly.

FOs has no backend. For the headless tests this server imitates the two public
services the device talks to, so tests never touch the real internet:

    /lichess/api/...                          Lichess (Settings > Accounts > Lichess server)
    /direct/api.open-meteo.com/v1/forecast    Open-Meteo forecast
    /direct/geocoding-api.open-meteo.com/...  Open-Meteo place search

The simulator sends public API calls to /direct/<host>/<path> when
FOS_SIM_DIRECT_BASE points here. Standard library only.

    python tools/simtest/mock/server.py --port 8799
"""

import argparse
import datetime as dt
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LOCK = threading.Lock()

GAMES = {
    "g1": {"id": "g1", "opponent": "marta_k", "color": "white", "speed": "correspondence",
           "moves": ["e2e4", "c7c5", "g1f3", "d7d6"], "isMyTurn": True},
    "g2": {"id": "g2", "opponent": "luca88", "color": "black", "speed": "classical",
           "moves": ["d2d4", "g8f6", "c2c4"], "isMyTurn": True},
}

DAILY_PUZZLE = {
    "game": {"pgn": "e4 e5 Nf3 Nc6 Bc4 Nd4 Nxe5 Qg5"},
    "puzzle": {"id": "fos01", "rating": 1420, "initialPly": 7,
               "fen": "r1b1kbnr/pppp1ppp/8/4N1q1/2BnP3/8/PPPP1PPP/RNBQK2R w KQkq - 2 5",
               "solution": ["e5f7", "g5g2", "h1f1", "g2e4", "c4e2", "d4f3"], "themes": ["fork"]},
}

FORECAST = {"current": {"temperature_2m": 12.3, "relative_humidity_2m": 81, "weather_code": 61},
            "daily": {"temperature_2m_max": [15.1], "temperature_2m_min": [7.4]}}


class Handler(BaseHTTPRequestHandler):
    server_version = "FOsTestMock/1.0"

    def log_message(self, fmt, *args):
        print("%s  %s" % (self.address_string(), fmt % args))

    def send_json(self, code, body):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[-1] for k, v in parse_qs(url.query).items()}
        path = url.path.rstrip("/") or "/"
        if path.startswith("/lichess/"):
            return self.lichess_get(path[len("/lichess"):])
        if path.startswith("/direct/"):
            return self.direct_get(path[len("/direct/"):], q)
        self.send_json(404, {"error": "not found", "path": path})

    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/")
        parts = path.strip("/").split("/")
        # /lichess/api/board/game/{id}/move/{uci}
        if len(parts) == 7 and parts[:4] == ["lichess", "api", "board", "game"] and parts[5] == "move":
            with LOCK:
                game = GAMES.get(parts[4])
                if not game:
                    return self.send_json(404, {"error": "no such game"})
                print("  <- device sent lichess move: %s" % json.dumps({"game": parts[4], "move": parts[6]}))
                game["moves"].append(parts[6])
                game["isMyTurn"] = False
            return self.send_json(200, {"ok": True})
        self.send_json(404, {"error": "not found", "path": path})

    def lichess_get(self, path):
        if path == "/api/account":
            return self.send_json(200, {"id": "fos_player", "username": "fos_player"})
        if path == "/api/account/playing":
            with LOCK:
                games = [{"gameId": g["id"], "opponent": {"username": g["opponent"]}, "color": g["color"],
                          "speed": g["speed"], "isMyTurn": g["isMyTurn"], "moves": " ".join(g["moves"])}
                         for g in GAMES.values()]
            return self.send_json(200, {"nowPlaying": games})
        if path == "/api/puzzle/daily":
            return self.send_json(200, DAILY_PUZZLE)
        return self.send_json(404, {"error": "not found"})

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
            return self.send_json(200, dict(FORECAST, generated=dt.datetime.now().isoformat()))
        return self.send_json(404, {"error": "not found", "host": host, "path": path})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8799)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("FOs test mock on http://%s:%d/" % (args.host, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
