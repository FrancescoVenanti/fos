#pragma once

#include <cstdint>

#include "ChessRules.h"
#include "fos/ui/Canvas.h"

namespace fos::chess {

// Draws a board: light squares white, dark squares light gray, pieces as 1-bit
// silhouette + ink masks. Every square is a hit area (action, value = square).
struct BoardStyle {
  bool flipped = false;  // black at the bottom
  int selected = -1;     // square with a thick frame
  uint64_t targets = 0;  // legal destinations of the selected piece (dots)
  int lastFrom = -1;     // last move, corner marks
  int lastTo = -1;
  int marked = -1;  // trainer: square filled black
  bool showPieces = true;
  bool coordinates = true;  // ranks left of the board, files below it (COORD_H)
  int checkSquare = -1;     // king in check, filled dark gray
};

constexpr int SQUARE = 54;
constexpr int BOARD = SQUARE * 8;
constexpr int COORD_H = 18;  // file letters row under the board

// r is the board area (BOARD x BOARD) without its frame; the 3 px frame is
// drawn outside it.
void drawBoard(ui::Canvas& c, const ui::Rect& r, const Position& pos, const BoardStyle& style, uint16_t action);
// One piece (type 1-6) at (x, y), 48 or 32 px.
void drawPiece(ui::Canvas& c, int x, int y, int type, bool white, int size = 48);
// Square under a board-relative point, honoring flip.
int squareAt(const ui::Rect& r, bool flipped, int x, int y);
// "e4".
void squareName(int sq, char* out);

}  // namespace fos::chess
