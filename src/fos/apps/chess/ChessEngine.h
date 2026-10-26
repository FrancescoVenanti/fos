#pragma once

#include <cstdint>
#include <vector>

#include "ChessRules.h"

namespace fos::chess {

// A small alpha-beta engine for the "vs bot" mode: iterative deepening,
// quiescence search on captures, MVV-LVA ordering, material plus
// piece-square evaluation. Moves are generated into one buffer allocated
// once (no per-ply arrays on the stack: the main loop task has 8 KB).
//
// Strength levels 1-5 set the depth, the time budget and how much noise is
// added to the root scores (so low levels make human-like mistakes).
class Engine {
 public:
  static constexpr int MIN_LEVEL = 1;
  static constexpr int MAX_LEVEL = 5;

  Engine();

  // Best move for the side to move; invalid Move if there is none.
  // Blocks for at most the level's time budget.
  Move think(const Position& pos, int level);

  int lastDepth() const { return lastDepth_; }
  uint32_t lastNodes() const { return nodes_; }

 private:
  int search(const Position& pos, int depth, int alpha, int beta, int ply);
  int quiesce(const Position& pos, int alpha, int beta, int ply);
  int order(const Position& pos, int start, int count, const Move* first);
  bool timeUp();

  std::vector<Move> buffer_;
  std::vector<int> scores_;
  int top_ = 0;
  uint32_t nodes_ = 0;
  unsigned long deadline_ = 0;
  bool aborted_ = false;
  int lastDepth_ = 0;
};

// Static evaluation from the side to move's point of view (centipawns).
int evaluate(const Position& pos);

}  // namespace fos::chess
