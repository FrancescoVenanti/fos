#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::chess {

// Chess rules: a 64-square mailbox board (a1 = 0, h1 = 7, a8 = 56), legal
// move generation with castling, en passant and promotion, FEN, UCI and SAN.

enum Piece : int8_t { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };

// Board values: +piece for white, -piece for black.
inline int pieceType(const int8_t v) { return v < 0 ? -v : v; }
inline bool isWhite(const int8_t v) { return v > 0; }
inline int fileOf(const int sq) { return sq & 7; }
inline int rankOf(const int sq) { return sq >> 3; }

struct Move {
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t promo = 0;  // Piece type, 0 = none
  uint8_t flags = 0;  // MOVE_* below

  bool operator==(const Move& o) const { return from == o.from && to == o.to && promo == o.promo; }
  bool valid() const { return from != to; }
};

constexpr uint8_t MOVE_CAPTURE = 1;
constexpr uint8_t MOVE_EN_PASSANT = 2;
constexpr uint8_t MOVE_CASTLE = 4;
constexpr uint8_t MOVE_DOUBLE = 8;

// Most moves any legal position can have.
constexpr int MAX_MOVES = 256;

constexpr uint8_t CASTLE_WK = 1;
constexpr uint8_t CASTLE_WQ = 2;
constexpr uint8_t CASTLE_BK = 4;
constexpr uint8_t CASTLE_BQ = 8;

struct Position {
  int8_t sq[64] = {};
  bool whiteToMove = true;
  uint8_t castle = 0;
  int8_t ep = -1;  // en passant target square, -1 = none
  int halfmove = 0;
  int fullmove = 1;

  void setStart();
  bool fromFen(const std::string& fen);
  std::string fen() const;

  // Pseudo-legal moves (may leave the king in check) into out; returns the count.
  int generate(Move* out, bool capturesOnly = false) const;
  // Legal moves only.
  int legal(Move* out) const;
  std::vector<Move> legalMoves() const;
  bool isLegal(const Move& m) const;

  bool attacked(int square, bool byWhite) const;
  int kingSquare(bool white) const;
  bool inCheck() const { return attacked(kingSquare(whiteToMove), !whiteToMove); }

  // Plays a pseudo-legal move; returns false (position unchanged) if it would
  // leave the mover's king in check.
  bool make(const Move& m);

  uint64_t hash() const;

  std::string uci(const Move& m) const;
  std::string san(const Move& m) const;
  // Parse a move in UCI ("e2e4", "e7e8q") or SAN ("Nf3", "exd5", "O-O");
  // only legal moves are accepted.
  bool parseUci(const std::string& text, Move& out) const;
  bool parseSan(const std::string& text, Move& out) const;
};

enum class Result : uint8_t { Ongoing, Checkmate, Stalemate, FiftyMoves, Material, Repetition, Resigned, Timeout };

// A game: the start position, the moves played and the positions seen (for
// threefold repetition).
class Game {
 public:
  Game() { reset(); }

  void reset();
  bool resetFromFen(const std::string& fen);
  bool play(const Move& m);
  bool playUci(const std::string& uci);
  bool undo();

  const Position& position() const { return pos_; }
  const Position& start() const { return start_; }
  const std::vector<Move>& moves() const { return moves_; }
  // All moves as space-separated UCI.
  std::string uciMoves() const;
  // Replays space-separated UCI moves from the start; false at the first bad one.
  bool playUciList(const std::string& list);

  Result result() const;
  // Set when the game ended by resignation or time; cleared by reset().
  void setOver(Result r, bool whiteWon);
  bool whiteWon() const;  // only meaningful for a decisive result
  bool over() const { return result() != Result::Ongoing; }

  // "1. e4 e5 2. Nf3" for the last few moves, at most maxChars long.
  std::string recentSan(size_t maxChars) const;

 private:
  void rebuild();

  Position start_;
  Position pos_;
  std::vector<Move> moves_;
  std::vector<uint64_t> hashes_;  // one per position, start included
  Result forced_ = Result::Ongoing;
  bool forcedWhiteWon_ = false;
};

const char* resultText(Result r);

}  // namespace fos::chess
