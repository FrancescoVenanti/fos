#pragma once

#include <cstdint>

namespace fos::puzzles {

// Sudoku generated on the device: a random full grid, then clues removed
// while the puzzle keeps exactly one solution. Difficulty sets how many clues
// stay (easy 40, medium 32, hard 27, expert 24, best effort).
// Cells hold 0 (empty) or 1-9; index = row * 9 + col.

enum class Level : uint8_t { Easy = 0, Medium, Hard, Expert };

struct Grid {
  uint8_t cell[81] = {};
};

// Solutions found, up to limit (2 is enough to test uniqueness).
int countSolutions(Grid g, int limit);
bool solve(Grid& g);
// seed makes generation repeatable (tests); returns the clue count.
int generate(Level level, uint32_t seed, Grid& puzzle, Grid& solution);
// True when the value conflicts with another cell in its row, column or box.
bool conflicts(const Grid& g, int index, uint8_t value);

}  // namespace fos::puzzles
