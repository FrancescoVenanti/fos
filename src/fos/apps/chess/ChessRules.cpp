#include "ChessRules.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace fos::chess {
namespace {

constexpr const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr const char* PIECE_CHARS = ".pnbrqk";

constexpr int KNIGHT_STEPS[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING_STEPS[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int ROOK_DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BISHOP_DIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

inline bool onBoard(const int f, const int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

int8_t pieceFromChar(const char c) {
  const char* p = strchr(PIECE_CHARS, tolower(static_cast<unsigned char>(c)));
  if (!p || *p == '.') return 0;
  const int8_t t = static_cast<int8_t>(p - PIECE_CHARS);
  return isupper(static_cast<unsigned char>(c)) ? t : static_cast<int8_t>(-t);
}

void squareName(const int sq, char* out) {
  out[0] = static_cast<char>('a' + fileOf(sq));
  out[1] = static_cast<char>('1' + rankOf(sq));
  out[2] = '\0';
}

int parseSquare(const char* s) {
  if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
  return (s[1] - '1') * 8 + (s[0] - 'a');
}

// FNV-1a over everything that defines a position for repetition.
uint64_t fnv(const Position& p) {
  uint64_t h = 1469598103934665603ULL;
  const auto mix = [&h](const uint8_t b) {
    h ^= b;
    h *= 1099511628211ULL;
  };
  for (const int8_t v : p.sq) mix(static_cast<uint8_t>(v));
  mix(p.whiteToMove ? 1 : 0);
  mix(p.castle);
  mix(static_cast<uint8_t>(p.ep));
  return h;
}

}  // namespace

void Position::setStart() { fromFen(START_FEN); }

bool Position::fromFen(const std::string& fen) {
  Position p;
  size_t i = 0;
  int rank = 7;
  int file = 0;
  for (; i < fen.size() && fen[i] != ' '; ++i) {
    const char c = fen[i];
    if (c == '/') {
      --rank;
      file = 0;
    } else if (c >= '1' && c <= '8') {
      file += c - '0';
    } else {
      const int8_t v = pieceFromChar(c);
      if (!v || rank < 0 || file > 7) return false;
      p.sq[rank * 8 + file++] = v;
    }
  }
  if (rank != 0) return false;
  char side = 'w';
  char castling[8] = "-";
  char ep[4] = "-";
  int half = 0;
  int full = 1;
  const int n = sscanf(fen.c_str() + i, " %c %7s %3s %d %d", &side, castling, ep, &half, &full);
  if (n < 1) return false;
  p.whiteToMove = side != 'b';
  for (const char* c = castling; *c; ++c) {
    if (*c == 'K') p.castle |= CASTLE_WK;
    if (*c == 'Q') p.castle |= CASTLE_WQ;
    if (*c == 'k') p.castle |= CASTLE_BK;
    if (*c == 'q') p.castle |= CASTLE_BQ;
  }
  p.ep = static_cast<int8_t>(strcmp(ep, "-") == 0 ? -1 : parseSquare(ep));
  p.halfmove = half;
  p.fullmove = full > 0 ? full : 1;
  if (p.kingSquare(true) < 0 || p.kingSquare(false) < 0) return false;
  *this = p;
  return true;
}

std::string Position::fen() const {
  std::string out;
  for (int r = 7; r >= 0; --r) {
    int empty = 0;
    for (int f = 0; f < 8; ++f) {
      const int8_t v = sq[r * 8 + f];
      if (!v) {
        ++empty;
        continue;
      }
      if (empty) out += static_cast<char>('0' + empty), empty = 0;
      const char c = PIECE_CHARS[pieceType(v)];
      out += isWhite(v) ? static_cast<char>(toupper(c)) : c;
    }
    if (empty) out += static_cast<char>('0' + empty);
    if (r) out += '/';
  }
  out += whiteToMove ? " w " : " b ";
  if (!castle) out += '-';
  if (castle & CASTLE_WK) out += 'K';
  if (castle & CASTLE_WQ) out += 'Q';
  if (castle & CASTLE_BK) out += 'k';
  if (castle & CASTLE_BQ) out += 'q';
  char tail[24];
  char epName[3] = "-";
  if (ep >= 0) squareName(ep, epName);
  snprintf(tail, sizeof(tail), " %s %d %d", epName, halfmove, fullmove);
  return out + tail;
}

int Position::kingSquare(const bool white) const {
  const int8_t k = white ? KING : -KING;
  for (int i = 0; i < 64; ++i) {
    if (sq[i] == k) return i;
  }
  return -1;
}

bool Position::attacked(const int square, const bool byWhite) const {
  if (square < 0) return false;
  const int f = fileOf(square);
  const int r = rankOf(square);
  const int sign = byWhite ? 1 : -1;
  // Pawns attack diagonally forward, so look one rank behind the target.
  const int pr = r - sign;
  for (const int df : {-1, 1}) {
    if (onBoard(f + df, pr) && sq[pr * 8 + f + df] == sign * PAWN) return true;
  }
  for (const auto& s : KNIGHT_STEPS) {
    if (onBoard(f + s[0], r + s[1]) && sq[(r + s[1]) * 8 + f + s[0]] == sign * KNIGHT) return true;
  }
  for (const auto& s : KING_STEPS) {
    if (onBoard(f + s[0], r + s[1]) && sq[(r + s[1]) * 8 + f + s[0]] == sign * KING) return true;
  }
  for (const auto& d : ROOK_DIRS) {
    for (int x = f + d[0], y = r + d[1]; onBoard(x, y); x += d[0], y += d[1]) {
      const int8_t v = sq[y * 8 + x];
      if (!v) continue;
      if (v == sign * ROOK || v == sign * QUEEN) return true;
      break;
    }
  }
  for (const auto& d : BISHOP_DIRS) {
    for (int x = f + d[0], y = r + d[1]; onBoard(x, y); x += d[0], y += d[1]) {
      const int8_t v = sq[y * 8 + x];
      if (!v) continue;
      if (v == sign * BISHOP || v == sign * QUEEN) return true;
      break;
    }
  }
  return false;
}

int Position::generate(Move* out, const bool capturesOnly) const {
  int n = 0;
  const int sign = whiteToMove ? 1 : -1;
  const auto add = [&](const int from, const int to, const uint8_t flags, const uint8_t promo = 0) {
    out[n++] = {static_cast<uint8_t>(from), static_cast<uint8_t>(to), promo, flags};
  };
  for (int from = 0; from < 64; ++from) {
    const int8_t v = sq[from];
    if (!v || (v > 0) != whiteToMove) continue;
    const int f = fileOf(from);
    const int r = rankOf(from);
    switch (pieceType(v)) {
      case PAWN: {
        const int fwd = r + sign;
        const bool promoRank = fwd == (whiteToMove ? 7 : 0);
        const auto pawnMove = [&](const int to, const uint8_t flags) {
          if (promoRank) {
            for (const uint8_t p : {QUEEN, KNIGHT, ROOK, BISHOP}) add(from, to, flags, p);
          } else {
            add(from, to, flags);
          }
        };
        if (onBoard(f, fwd) && !sq[fwd * 8 + f] && (!capturesOnly || promoRank)) {
          pawnMove(fwd * 8 + f, 0);
          const int startRank = whiteToMove ? 1 : 6;
          const int two = r + 2 * sign;
          if (!capturesOnly && r == startRank && !sq[two * 8 + f]) add(from, two * 8 + f, MOVE_DOUBLE);
        }
        for (const int df : {-1, 1}) {
          if (!onBoard(f + df, fwd)) continue;
          const int to = fwd * 8 + f + df;
          if (sq[to] && (sq[to] > 0) != whiteToMove) {
            pawnMove(to, MOVE_CAPTURE);
          } else if (to == ep) {
            add(from, to, MOVE_CAPTURE | MOVE_EN_PASSANT);
          }
        }
        break;
      }
      case KNIGHT:
      case KING: {
        const auto& steps = pieceType(v) == KNIGHT ? KNIGHT_STEPS : KING_STEPS;
        for (const auto& s : steps) {
          const int x = f + s[0];
          const int y = r + s[1];
          if (!onBoard(x, y)) continue;
          const int8_t t = sq[y * 8 + x];
          if (t && (t > 0) == whiteToMove) continue;
          if (capturesOnly && !t) continue;
          add(from, y * 8 + x, t ? MOVE_CAPTURE : 0);
        }
        if (pieceType(v) == KING && !capturesOnly) {
          // Castling: empty path, king not in, through or into check.
          const int home = whiteToMove ? 4 : 60;
          const uint8_t k = whiteToMove ? CASTLE_WK : CASTLE_BK;
          const uint8_t q = whiteToMove ? CASTLE_WQ : CASTLE_BQ;
          if (from == home && !attacked(home, !whiteToMove)) {
            if ((castle & k) && !sq[home + 1] && !sq[home + 2] && sq[home + 3] == sign * ROOK &&
                !attacked(home + 1, !whiteToMove) && !attacked(home + 2, !whiteToMove)) {
              add(from, home + 2, MOVE_CASTLE);
            }
            if ((castle & q) && !sq[home - 1] && !sq[home - 2] && !sq[home - 3] && sq[home - 4] == sign * ROOK &&
                !attacked(home - 1, !whiteToMove) && !attacked(home - 2, !whiteToMove)) {
              add(from, home - 2, MOVE_CASTLE);
            }
          }
        }
        break;
      }
      default: {
        const int type = pieceType(v);
        const auto slide = [&](const int (*dirs)[2]) {
          for (int d = 0; d < 4; ++d) {
            for (int x = f + dirs[d][0], y = r + dirs[d][1]; onBoard(x, y); x += dirs[d][0], y += dirs[d][1]) {
              const int8_t t = sq[y * 8 + x];
              if (t && (t > 0) == whiteToMove) break;
              if (!capturesOnly || t) add(from, y * 8 + x, t ? MOVE_CAPTURE : 0);
              if (t) break;
            }
          }
        };
        if (type == ROOK || type == QUEEN) slide(ROOK_DIRS);
        if (type == BISHOP || type == QUEEN) slide(BISHOP_DIRS);
        break;
      }
    }
  }
  return n;
}

bool Position::make(const Move& m) {
  Position next = *this;
  const int8_t piece = next.sq[m.from];
  const int sign = whiteToMove ? 1 : -1;
  const bool capture = next.sq[m.to] != 0 || (m.flags & MOVE_EN_PASSANT);
  next.sq[m.to] = m.promo ? static_cast<int8_t>(sign * m.promo) : piece;
  next.sq[m.from] = 0;
  if (m.flags & MOVE_EN_PASSANT) next.sq[m.to - 8 * sign] = 0;
  if (m.flags & MOVE_CASTLE) {
    const bool kingSide = m.to > m.from;
    const int rookFrom = kingSide ? m.from + 3 : m.from - 4;
    const int rookTo = kingSide ? m.from + 1 : m.from - 1;
    next.sq[rookTo] = next.sq[rookFrom];
    next.sq[rookFrom] = 0;
  }
  next.ep = static_cast<int8_t>(
      pieceType(piece) == PAWN && (m.to - m.from == 16 || m.from - m.to == 16) ? (m.from + m.to) / 2 : -1);
  // Moving from or capturing on a king or rook home square ends those rights.
  for (const int s : {static_cast<int>(m.from), static_cast<int>(m.to)}) {
    if (s == 4) next.castle &= ~(CASTLE_WK | CASTLE_WQ);
    if (s == 60) next.castle &= ~(CASTLE_BK | CASTLE_BQ);
    if (s == 7) next.castle &= ~CASTLE_WK;
    if (s == 0) next.castle &= ~CASTLE_WQ;
    if (s == 63) next.castle &= ~CASTLE_BK;
    if (s == 56) next.castle &= ~CASTLE_BQ;
  }
  next.halfmove = pieceType(piece) == PAWN || capture ? 0 : halfmove + 1;
  if (!whiteToMove) ++next.fullmove;
  next.whiteToMove = !whiteToMove;
  if (next.attacked(next.kingSquare(whiteToMove), next.whiteToMove)) return false;
  *this = next;
  return true;
}

int Position::legal(Move* out) const {
  Move all[MAX_MOVES];
  const int count = generate(all);
  int n = 0;
  for (int i = 0; i < count; ++i) {
    Position p = *this;
    if (p.make(all[i])) out[n++] = all[i];
  }
  return n;
}

std::vector<Move> Position::legalMoves() const {
  Move moves[MAX_MOVES];
  const int n = legal(moves);
  return std::vector<Move>(moves, moves + n);
}

bool Position::isLegal(const Move& m) const {
  Move moves[MAX_MOVES];
  const int n = legal(moves);
  for (int i = 0; i < n; ++i) {
    if (moves[i] == m) return true;
  }
  return false;
}

uint64_t Position::hash() const { return fnv(*this); }

std::string Position::uci(const Move& m) const {
  char out[6];
  squareName(m.from, out);
  squareName(m.to, out + 2);
  out[4] = m.promo ? PIECE_CHARS[m.promo] : '\0';
  out[5] = '\0';
  return out;
}

std::string Position::san(const Move& m) const {
  std::string out;
  const int8_t piece = sq[m.from];
  const int type = pieceType(piece);
  char to[3];
  squareName(m.to, to);
  if (m.flags & MOVE_CASTLE) {
    out = m.to > m.from ? "O-O" : "O-O-O";
  } else if (type == PAWN) {
    if (m.flags & MOVE_CAPTURE) {
      out += static_cast<char>('a' + fileOf(m.from));
      out += 'x';
    }
    out += to;
    if (m.promo) {
      out += '=';
      out += static_cast<char>(toupper(PIECE_CHARS[m.promo]));
    }
  } else {
    out += static_cast<char>(toupper(PIECE_CHARS[type]));
    // Disambiguate between same-type pieces that can reach the same square.
    Move moves[MAX_MOVES];
    const int n = legal(moves);
    bool clash = false;
    bool sameFile = false;
    bool sameRank = false;
    for (int i = 0; i < n; ++i) {
      const Move& o = moves[i];
      if (o.to != m.to || o.from == m.from || sq[o.from] != piece) continue;
      clash = true;
      if (fileOf(o.from) == fileOf(m.from)) sameFile = true;
      if (rankOf(o.from) == rankOf(m.from)) sameRank = true;
    }
    if (clash) {
      if (!sameFile) {
        out += static_cast<char>('a' + fileOf(m.from));
      } else if (!sameRank) {
        out += static_cast<char>('1' + rankOf(m.from));
      } else {
        char from[3];
        squareName(m.from, from);
        out += from;
      }
    }
    if (m.flags & MOVE_CAPTURE) out += 'x';
    out += to;
  }
  Position after = *this;
  if (after.make(m) && after.inCheck()) {
    Move replies[MAX_MOVES];
    out += after.legal(replies) == 0 ? '#' : '+';
  }
  return out;
}

bool Position::parseUci(const std::string& text, Move& out) const {
  if (text.size() < 4) return false;
  const int from = parseSquare(text.c_str());
  const int to = parseSquare(text.c_str() + 2);
  if (from < 0 || to < 0) return false;
  uint8_t promo = 0;
  if (text.size() >= 5) {
    const int8_t p = pieceFromChar(text[4]);
    promo = static_cast<uint8_t>(pieceType(p));
  }
  Move moves[MAX_MOVES];
  const int n = legal(moves);
  for (int i = 0; i < n; ++i) {
    if (moves[i].from == from && moves[i].to == to && moves[i].promo == promo) {
      out = moves[i];
      return true;
    }
  }
  return false;
}

bool Position::parseSan(const std::string& text, Move& out) const {
  std::string want;
  for (const char c : text) {
    if (c != '+' && c != '#' && c != '!' && c != '?') want += c == '0' ? 'O' : c;
  }
  if (want.empty()) return false;
  Move moves[MAX_MOVES];
  const int n = legal(moves);
  for (int i = 0; i < n; ++i) {
    std::string s = san(moves[i]);
    while (!s.empty() && (s.back() == '+' || s.back() == '#')) s.pop_back();
    if (s == want) {
      out = moves[i];
      return true;
    }
  }
  return false;
}

void Game::reset() {
  start_.setStart();
  rebuild();
}

bool Game::resetFromFen(const std::string& fen) {
  Position p;
  if (!p.fromFen(fen)) return false;
  start_ = p;
  rebuild();
  return true;
}

void Game::rebuild() {
  pos_ = start_;
  moves_.clear();
  hashes_.assign(1, pos_.hash());
  forced_ = Result::Ongoing;
}

bool Game::play(const Move& m) {
  if (over() || !pos_.isLegal(m)) return false;
  Move full = m;
  // Fill in flags from the generator so callers can pass from/to/promo only.
  for (const Move& legal : pos_.legalMoves()) {
    if (legal == m) full = legal;
  }
  pos_.make(full);
  moves_.push_back(full);
  hashes_.push_back(pos_.hash());
  return true;
}

bool Game::playUci(const std::string& uci) {
  Move m;
  return pos_.parseUci(uci, m) && play(m);
}

bool Game::undo() {
  if (moves_.empty()) return false;
  std::vector<Move> keep(moves_.begin(), moves_.end() - 1);
  rebuild();
  for (const Move& m : keep) play(m);
  return true;
}

std::string Game::uciMoves() const {
  std::string out;
  Position p = start_;
  for (const Move& m : moves_) {
    if (!out.empty()) out += ' ';
    out += p.uci(m);
    p.make(m);
  }
  return out;
}

bool Game::playUciList(const std::string& list) {
  size_t i = 0;
  while (i < list.size()) {
    while (i < list.size() && list[i] == ' ') ++i;
    size_t j = i;
    while (j < list.size() && list[j] != ' ') ++j;
    if (j > i && !playUci(list.substr(i, j - i))) return false;
    i = j;
  }
  return true;
}

void Game::setOver(const Result r, const bool whiteWon) {
  forced_ = r;
  forcedWhiteWon_ = whiteWon;
}

Result Game::result() const {
  if (forced_ != Result::Ongoing) return forced_;
  Move moves[MAX_MOVES];
  if (pos_.legal(moves) == 0) return pos_.inCheck() ? Result::Checkmate : Result::Stalemate;
  if (pos_.halfmove >= 100) return Result::FiftyMoves;
  int seen = 0;
  for (const uint64_t h : hashes_) seen += h == hashes_.back() ? 1 : 0;
  if (seen >= 3) return Result::Repetition;
  // Insufficient material: bare kings, or one minor piece.
  int minors = 0;
  for (const int8_t v : pos_.sq) {
    const int t = pieceType(v);
    if (t == PAWN || t == ROOK || t == QUEEN) return Result::Ongoing;
    if (t == KNIGHT || t == BISHOP) ++minors;
  }
  return minors <= 1 ? Result::Material : Result::Ongoing;
}

bool Game::whiteWon() const {
  if (forced_ != Result::Ongoing) return forcedWhiteWon_;
  // Checkmate: the side to move lost.
  return !pos_.whiteToMove;
}

std::string Game::recentSan(const size_t maxChars) const {
  // Build "N. white black" tokens, then keep the newest that fit.
  std::vector<std::string> tokens;
  Position p = start_;
  for (const Move& m : moves_) {
    std::string t;
    if (p.whiteToMove) {
      t = std::to_string(p.fullmove) + ". ";
    } else if (tokens.empty()) {
      t = std::to_string(p.fullmove) + "... ";
    }
    t += p.san(m);
    tokens.push_back(std::move(t));
    p.make(m);
  }
  std::string out;
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
    const std::string next = out.empty() ? *it : *it + " " + out;
    if (next.size() > maxChars) break;
    out = next;
  }
  return out;
}

const char* resultText(const Result r) {
  switch (r) {
    case Result::Checkmate:
      return "Checkmate";
    case Result::Stalemate:
      return "Stalemate";
    case Result::FiftyMoves:
      return "Draw by the 50-move rule";
    case Result::Material:
      return "Draw, not enough material";
    case Result::Repetition:
      return "Draw by repetition";
    case Result::Resigned:
      return "Resigned";
    case Result::Timeout:
      return "Out of time";
    case Result::Ongoing:
      break;
  }
  return "";
}

}  // namespace fos::chess
