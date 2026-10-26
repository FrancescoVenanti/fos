#include "ChessEngine.h"

#include <Arduino.h>
#include <esp_random.h>

#include <algorithm>

namespace fos::chess {
namespace {

constexpr int INF = 1000000;
constexpr int MATE = 100000;
constexpr int MAX_PLY = 48;
constexpr int VALUE[7] = {0, 100, 320, 330, 500, 900, 0};

struct Level {
  int depth;
  unsigned long timeMs;
  int noise;  // centipawns of random noise on root moves
};
constexpr Level LEVELS[Engine::MAX_LEVEL] = {
    {1, 300, 220}, {2, 600, 90}, {3, 1200, 30}, {4, 2500, 0}, {6, 4000, 0},
};

// Piece-square tables from white's point of view, a1 = index 0. Black uses
// the mirrored square (sq ^ 56).
constexpr int8_t PST_PAWN[64] = {0,  0,  0,   0,  0,  0,   0,  0,  5,  10, 10, -20, -20, 10, 10, 5,
                                 5,  -5, -10, 0,  0,  -10, -5, 5,  0,  0,  0,  20,  20,  0,  0,  0,
                                 5,  5,  10,  25, 25, 10,  5,  5,  10, 10, 20, 30,  30,  20, 10, 10,
                                 50, 50, 50,  50, 50, 50,  50, 50, 0,  0,  0,  0,   0,   0,  0,  0};
constexpr int8_t PST_KNIGHT[64] = {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   5,   5,   0,   -20, -40,
                                   -30, 5,   10,  15,  15,  10,  5,   -30, -30, 0,   15,  20,  20,  15,  0,   -30,
                                   -30, 5,   15,  20,  20,  15,  5,   -30, -30, 0,   10,  15,  15,  10,  0,   -30,
                                   -40, -20, 0,   0,   0,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};
constexpr int8_t PST_BISHOP[64] = {-20, -10, -10, -10, -10, -10, -10, -20, -10, 5,   0,   0,   0,   0,   5,   -10,
                                   -10, 10,  10,  10,  10,  10,  10,  -10, -10, 0,   10,  10,  10,  10,  0,   -10,
                                   -10, 5,   5,   10,  10,  5,   5,   -10, -10, 0,   5,   10,  10,  5,   0,   -10,
                                   -10, 0,   0,   0,   0,   0,   0,   -10, -20, -10, -10, -10, -10, -10, -10, -20};
constexpr int8_t PST_ROOK[64] = {0, 0,  0,  5,  5, 0,  0,  0,  -5, 0,  0,  0, 0, 0, 0, -5, -5, 0,  0,  0, 0, 0,
                                 0, -5, -5, 0,  0, 0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0,  0,  -5, -5, 0, 0, 0,
                                 0, 0,  0,  -5, 5, 10, 10, 10, 10, 10, 10, 5, 0, 0, 0, 0,  0,  0,  0,  0};
constexpr int8_t PST_QUEEN[64] = {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0,   5,   0,  0,  0,   0,   -10,
                                  -10, 5,   5,   5,  5,  5,   0,   -10, 0,   0,   5,   5,  5,  5,   0,   -5,
                                  -5,  0,   5,   5,  5,  5,   0,   -5,  -10, 0,   5,   5,  5,  5,   0,   -10,
                                  -10, 0,   0,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20};
constexpr int8_t PST_KING[64] = {20,  30,  10,  0,   0,   10,  30,  20,  20,  20,  0,   0,   0,   0,   20,  20,
                                 -10, -20, -20, -20, -20, -20, -20, -10, -20, -30, -30, -40, -40, -30, -30, -20,
                                 -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                 -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30};
// Endgame king: towards the center.
constexpr int8_t PST_KING_END[64] = {-50, -30, -30, -30, -30, -30, -30, -50, -30, -30, 0,   0,   0,   0,   -30, -30,
                                     -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10, 30,  40,  40,  30,  -10, -30,
                                     -30, -10, 30,  40,  40,  30,  -10, -30, -30, -10, 20,  30,  30,  20,  -10, -30,
                                     -30, -20, -10, 0,   0,   -10, -20, -30, -50, -40, -30, -20, -20, -30, -40, -50};

const int8_t* table(const int type, const bool endgame) {
  switch (type) {
    case PAWN:
      return PST_PAWN;
    case KNIGHT:
      return PST_KNIGHT;
    case BISHOP:
      return PST_BISHOP;
    case ROOK:
      return PST_ROOK;
    case QUEEN:
      return PST_QUEEN;
    default:
      return endgame ? PST_KING_END : PST_KING;
  }
}

}  // namespace

int evaluate(const Position& pos) {
  // Endgame once the queens are off or little material is left.
  int nonPawn = 0;
  for (const int8_t v : pos.sq) {
    const int t = pieceType(v);
    if (t >= KNIGHT && t <= QUEEN) nonPawn += VALUE[t];
  }
  const bool endgame = nonPawn <= 2600;
  int score = 0;
  for (int s = 0; s < 64; ++s) {
    const int8_t v = pos.sq[s];
    if (!v) continue;
    const int t = pieceType(v);
    const int idx = isWhite(v) ? s : s ^ 56;
    const int value = VALUE[t] + table(t, endgame)[idx];
    score += isWhite(v) ? value : -value;
  }
  return pos.whiteToMove ? score : -score;
}

Engine::Engine() {
  buffer_.resize(MAX_PLY * 64 + MAX_MOVES * 4);
  scores_.resize(buffer_.size());
}

bool Engine::timeUp() {
  if ((++nodes_ & 1023) == 0 && millis() > deadline_) aborted_ = true;
  return aborted_;
}

// Sorts moves[start, start+count) best first: the given move, then captures by
// most valuable victim / least valuable attacker, then promotions.
int Engine::order(const Position& pos, const int start, const int count, const Move* first) {
  for (int i = start; i < start + count; ++i) {
    const Move& m = buffer_[i];
    int s = 0;
    if (first && m == *first) {
      s = 1000000;
    } else if (m.flags & MOVE_CAPTURE) {
      const int victim = (m.flags & MOVE_EN_PASSANT) ? PAWN : pieceType(pos.sq[m.to]);
      s = 10000 + VALUE[victim] * 10 - VALUE[pieceType(pos.sq[m.from])] / 10;
    }
    if (m.promo) s += 9000 + VALUE[m.promo];
    scores_[i] = s;
  }
  // Insertion sort: lists are short.
  for (int i = start + 1; i < start + count; ++i) {
    const Move m = buffer_[i];
    const int s = scores_[i];
    int j = i - 1;
    while (j >= start && scores_[j] < s) {
      buffer_[j + 1] = buffer_[j];
      scores_[j + 1] = scores_[j];
      --j;
    }
    buffer_[j + 1] = m;
    scores_[j + 1] = s;
  }
  return count;
}

int Engine::quiesce(const Position& pos, int alpha, const int beta, const int ply) {
  if (timeUp()) return 0;
  const int stand = evaluate(pos);
  if (stand >= beta) return beta;
  if (stand > alpha) alpha = stand;
  if (ply >= MAX_PLY || top_ + MAX_MOVES > static_cast<int>(buffer_.size())) return alpha;
  const int start = top_;
  const int count = pos.generate(&buffer_[start], true);
  top_ += count;
  order(pos, start, count, nullptr);
  for (int i = start; i < start + count; ++i) {
    Position next = pos;
    if (!next.make(buffer_[i])) continue;
    const int score = -quiesce(next, -beta, -alpha, ply + 1);
    if (aborted_) break;
    if (score >= beta) {
      alpha = beta;
      break;
    }
    if (score > alpha) alpha = score;
  }
  top_ = start;
  return alpha;
}

int Engine::search(const Position& pos, const int depth, int alpha, const int beta, const int ply) {
  if (timeUp()) return 0;
  if (pos.halfmove >= 100) return 0;
  if (depth <= 0) return quiesce(pos, alpha, beta, ply);
  if (top_ + MAX_MOVES > static_cast<int>(buffer_.size())) return evaluate(pos);
  const int start = top_;
  const int count = pos.generate(&buffer_[start]);
  top_ += count;
  order(pos, start, count, nullptr);
  int legal = 0;
  const bool check = pos.inCheck();
  for (int i = start; i < start + count; ++i) {
    Position next = pos;
    if (!next.make(buffer_[i])) continue;
    ++legal;
    // Check extension: do not stop the search in the middle of a check.
    const int score = -search(next, depth - 1 + (next.inCheck() ? 1 : 0), -beta, -alpha, ply + 1);
    if (aborted_) break;
    if (score >= beta) {
      top_ = start;
      return beta;
    }
    if (score > alpha) alpha = score;
  }
  top_ = start;
  if (!legal && !aborted_) return check ? -MATE + ply : 0;
  return alpha;
}

Move Engine::think(const Position& pos, int level) {
  level = std::max(MIN_LEVEL, std::min(MAX_LEVEL, level));
  const Level& cfg = LEVELS[level - 1];
  nodes_ = 0;
  aborted_ = false;
  top_ = 0;
  deadline_ = millis() + cfg.timeMs;
  lastDepth_ = 0;

  Move root[MAX_MOVES];
  const int n = pos.legal(root);
  if (n == 0) return {};
  if (n == 1) return root[0];

  // Noise per root move, fixed for the whole search.
  int noise[MAX_MOVES] = {};
  for (int i = 0; i < n && cfg.noise > 0; ++i) {
    noise[i] = static_cast<int>(esp_random() % (2 * cfg.noise + 1)) - cfg.noise;
  }

  Move best = root[0];
  for (int depth = 1; depth <= cfg.depth; ++depth) {
    // Previous best first.
    for (int i = 0; i < n; ++i) {
      if (root[i] == best) {
        std::swap(root[0], root[i]);
        std::swap(noise[0], noise[i]);
        break;
      }
    }
    int alpha = -INF;
    Move bestHere = root[0];
    for (int i = 0; i < n; ++i) {
      Position next = pos;
      next.make(root[i]);
      const int score = -search(next, depth - 1, -INF, -alpha + cfg.noise + 1, 1) + noise[i];
      if (aborted_) break;
      if (score > alpha) {
        alpha = score;
        bestHere = root[i];
      }
    }
    if (aborted_) break;  // keep the last complete depth
    best = bestHere;
    lastDepth_ = depth;
    if (alpha > MATE - 100) break;  // found a mate
  }
  return best;
}

}  // namespace fos::chess
