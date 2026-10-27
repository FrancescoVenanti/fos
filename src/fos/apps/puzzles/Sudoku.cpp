#include "Sudoku.h"

namespace fos::puzzles {
namespace {

// Small xorshift so generation is repeatable from a seed.
struct Rng {
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int below(const int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

// Candidate bitmask (bits 1-9) for an empty cell.
uint16_t candidates(const Grid& g, const int index) {
  const int r = index / 9;
  const int c = index % 9;
  uint16_t used = 0;
  for (int i = 0; i < 9; ++i) {
    used |= 1u << g.cell[r * 9 + i];
    used |= 1u << g.cell[i * 9 + c];
  }
  const int br = r / 3 * 3;
  const int bc = c / 3 * 3;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) used |= 1u << g.cell[(br + i) * 9 + bc + j];
  }
  return static_cast<uint16_t>(~used & 0x3FE);
}

int popcount(uint16_t v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}

// Backtracking with the most constrained cell first. Fills g with the first
// solution; counts up to limit. Random order when rng is set (generation).
int search(Grid& g, const int limit, Rng* rng, Grid* first) {
  int best = -1;
  int bestCount = 10;
  uint16_t bestMask = 0;
  for (int i = 0; i < 81; ++i) {
    if (g.cell[i]) continue;
    const uint16_t m = candidates(g, i);
    const int n = popcount(m);
    if (n == 0) return 0;
    if (n < bestCount) {
      best = i;
      bestCount = n;
      bestMask = m;
      if (n == 1) break;
    }
  }
  if (best < 0) {
    if (first) *first = g;
    return 1;
  }
  uint8_t order[9];
  int k = 0;
  for (uint8_t v = 1; v <= 9; ++v) {
    if (bestMask & (1u << v)) order[k++] = v;
  }
  if (rng) {
    for (int i = k - 1; i > 0; --i) {
      const int j = rng->below(i + 1);
      const uint8_t t = order[i];
      order[i] = order[j];
      order[j] = t;
    }
  }
  int found = 0;
  for (int i = 0; i < k && found < limit; ++i) {
    g.cell[best] = order[i];
    found += search(g, limit - found, rng, found == 0 ? first : nullptr);
  }
  g.cell[best] = 0;
  return found;
}

}  // namespace

int countSolutions(Grid g, const int limit) { return search(g, limit, nullptr, nullptr); }

bool solve(Grid& g) {
  Grid out;
  if (search(g, 1, nullptr, &out) != 1) return false;
  g = out;
  return true;
}

bool conflicts(const Grid& g, const int index, const uint8_t value) {
  if (!value) return false;
  const int r = index / 9;
  const int c = index % 9;
  for (int i = 0; i < 9; ++i) {
    if (r * 9 + i != index && g.cell[r * 9 + i] == value) return true;
    if (i * 9 + c != index && g.cell[i * 9 + c] == value) return true;
  }
  const int br = r / 3 * 3;
  const int bc = c / 3 * 3;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const int k = (br + i) * 9 + bc + j;
      if (k != index && g.cell[k] == value) return true;
    }
  }
  return false;
}

int generate(const Level level, const uint32_t seed, Grid& puzzle, Grid& solution) {
  static constexpr int TARGET[] = {40, 32, 27, 24};
  Rng rng{seed ? seed : 0x9E3779B9u};
  Grid empty;
  search(empty, 1, &rng, &solution);
  puzzle = solution;
  // Remove cells in a random order (symmetric pairs), keeping one solution.
  int order[81];
  for (int i = 0; i < 81; ++i) order[i] = i;
  for (int i = 80; i > 0; --i) {
    const int j = rng.below(i + 1);
    const int t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  int clues = 81;
  const int target = TARGET[static_cast<int>(level)];
  for (int n = 0; n < 81 && clues > target; ++n) {
    const int a = order[n];
    const int b = 80 - a;
    if (!puzzle.cell[a]) continue;
    const uint8_t va = puzzle.cell[a];
    const uint8_t vb = puzzle.cell[b];
    puzzle.cell[a] = 0;
    puzzle.cell[b] = 0;
    if (countSolutions(puzzle, 2) != 1) {
      puzzle.cell[a] = va;
      puzzle.cell[b] = vb;
    } else {
      clues -= a == b ? 1 : 2;
    }
  }
  // Then single cells, for the harder levels.
  for (int n = 0; n < 81 && clues > target; ++n) {
    const int a = order[n];
    if (!puzzle.cell[a]) continue;
    const uint8_t va = puzzle.cell[a];
    puzzle.cell[a] = 0;
    if (countSolutions(puzzle, 2) != 1) {
      puzzle.cell[a] = va;
    } else {
      --clues;
    }
  }
  return clues;
}

}  // namespace fos::puzzles
