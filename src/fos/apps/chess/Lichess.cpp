#include "Lichess.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstring>

#include "ChessRules.h"
#include "fos/settings/FosSettings.h"
#include "fos/sync/Backend.h"

namespace fos::chess::lichess {
namespace {

std::string base() {
  std::string url = settings().lichessUrl.empty() ? "https://lichess.org" : settings().lichessUrl;
  if (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

bool call(const char* method, const std::string& path, net::Response& out, std::string& error) {
  if (!net::ensureWifi()) {
    error = "No Wi-Fi. Lichess needs a connection.";
    return false;
  }
  out = net::request(method, base() + path, "", settings().lichessToken, 10000);
  if (out.ok()) return true;
  if (out.status == 401) {
    error = "Lichess did not accept the token. Check Settings > Accounts.";
  } else if (out.status < 0) {
    error = "Cannot reach Lichess.";
  } else {
    error = "Lichess answered " + std::to_string(out.status) + ".";
  }
  LOG_INF("CHESS", "%s %s -> %d", method, path.c_str(), out.status);
  return false;
}

bool slowEnough(const char* speed) {
  return strcmp(speed, "correspondence") == 0 || strcmp(speed, "classical") == 0 || strcmp(speed, "rapid") == 0;
}

}  // namespace

bool hasToken() { return !settings().lichessToken.empty(); }

bool fetchGames(std::vector<OnlineGame>& out, int& hiddenFast, std::string& error) {
  out.clear();
  hiddenFast = 0;
  net::Response r;
  if (!call("GET", "/api/account/playing?nb=50", r, error)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    error = "Unexpected answer from Lichess.";
    return false;
  }
  for (JsonObjectConst g : doc["nowPlaying"].as<JsonArrayConst>()) {
    const char* speed = g["speed"] | "";
    if (!slowEnough(speed)) {
      ++hiddenFast;
      continue;
    }
    OnlineGame game;
    game.id = g["gameId"] | "";
    game.speed = speed;
    game.white = strcmp(g["color"] | "white", "white") == 0;
    game.myTurn = g["isMyTurn"] | false;
    game.opponent = g["opponent"]["username"] | "Opponent";
    // Lichess sends the current FEN; the test server sends the moves.
    if (!g["moves"].isNull()) {
      game.moves = g["moves"] | "";
    } else {
      game.fen = g["fen"] | "";
      // Lichess omits the move counters; the board needs a full FEN.
      if (!game.fen.empty() && strchr(game.fen.c_str(), ' ') == nullptr) {
        game.fen += game.myTurn == game.white ? " w - - 0 1" : " b - - 0 1";
      }
    }
    if (!game.id.empty()) out.push_back(std::move(game));
  }
  return true;
}

bool fetchPuzzle(OnlineGame& out, std::string& error) {
  net::Response r;
  if (!call("GET", "/api/puzzle/daily", r, error)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    error = "Unexpected answer from Lichess.";
    return false;
  }
  out = OnlineGame();
  out.id = doc["puzzle"]["id"] | "";
  out.rating = doc["puzzle"]["rating"] | 0;
  // The puzzle starts after move initialPly of the game (0-based), i.e. after
  // initialPly + 1 moves; the solution's first move is the user's.
  const int initialPly = doc["puzzle"]["initialPly"] | 0;
  const std::string pgn = doc["game"]["pgn"] | "";
  Game game;
  int ply = 0;
  size_t i = 0;
  while (i < pgn.size() && ply <= initialPly) {
    while (i < pgn.size() && pgn[i] == ' ') ++i;
    size_t j = i;
    while (j < pgn.size() && pgn[j] != ' ') ++j;
    const std::string token = pgn.substr(i, j - i);
    i = j;
    if (token.empty() || isdigit(static_cast<unsigned char>(token[0]))) continue;  // "12." move numbers
    Move m;
    if (!game.position().parseSan(token, m) || !game.play(m)) {
      error = "Could not read the puzzle.";
      return false;
    }
    ++ply;
  }
  out.moves = game.uciMoves();
  out.white = game.position().whiteToMove;
  out.myTurn = true;
  for (JsonVariantConst s : doc["puzzle"]["solution"].as<JsonArrayConst>()) out.solution.push_back(s | "");
  if (out.solution.empty()) {
    error = "The puzzle has no solution.";
    return false;
  }
  return true;
}

bool sendMove(const std::string& gameId, const std::string& uci, std::string& error) {
  net::Response r;
  return call("POST", "/api/board/game/" + gameId + "/move/" + uci, r, error);
}

}  // namespace fos::chess::lichess
