#pragma once

#include <string>
#include <vector>

namespace fos::chess {

// A Lichess game or puzzle as the board screen needs it.
struct OnlineGame {
  std::string id;
  std::string opponent;
  std::string speed;
  bool white = true;  // the user's color
  std::string fen;    // start position (empty = standard)
  std::string moves;  // UCI moves played from fen
  bool myTurn = false;
  std::vector<std::string> solution;  // puzzle: UCI moves, user first
  int rating = 0;
};

namespace lichess {

// FOs reaches Lichess directly (not through the backend) with the personal
// API token from Settings > Accounts. Requests block; callers show a loading
// state first. Each joins Wi-Fi if needed.

bool hasToken();

// Ongoing games the user can play on e-paper: correspondence, classical and
// rapid (blitz and faster are left out and counted in hiddenFast).
bool fetchGames(std::vector<OnlineGame>& out, int& hiddenFast, std::string& error);
// The daily puzzle, positioned at the move the user has to find.
bool fetchPuzzle(OnlineGame& out, std::string& error);
// Plays a move (UCI) in a game.
bool sendMove(const std::string& gameId, const std::string& uci, std::string& error);

}  // namespace lichess
}  // namespace fos::chess
