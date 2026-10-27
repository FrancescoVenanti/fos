#pragma once

#include <string>
#include <vector>

#include "Sudoku.h"
#include "WordGame.h"
#include "fos/ui/FosActivity.h"

namespace fos::puzzles {

// Puzzles (docs/PRODUCT.md): Sudoku generated on the device in four levels,
// and a five-letter word puzzle: the daily word from the sync, or the bundled
// pack's word of the day without it.
class PuzzlesActivity final : public ui::FosActivity {
 public:
  PuzzlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;

 private:
  enum Action : uint16_t { BACK = 1, SUDOKU, NEW_SUDOKU, WORD };

  void reload();

  std::string sudokuDetail_;
  std::string wordDetail_;
  bool sudokuSaved_ = false;
};

// Sudoku: tap a cell, then a digit. Notes mode writes pencil marks. Cells that
// clash with another in their row, column or box are marked. Saved as you play.
class SudokuActivity final : public ui::FosActivity {
 public:
  // level < 0: continue the saved game.
  SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int level);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;

 private:
  enum Action : uint16_t { BACK = 1, CELL, DIGIT, ERASE, NOTES, HINT };

  bool load();
  void save() const;
  void place(uint8_t digit);
  void updateTexts();

  int level_;
  Grid puzzle_;
  Grid solution_;
  Grid entries_;             // the player's digits (givens are in puzzle_)
  uint16_t notes_[81] = {};  // pencil marks, bits 1-9
  int selected_ = -1;
  bool notesMode_ = false;
  bool solved_ = false;
  int hints_ = 0;
  std::string status_;
};

// The word puzzle: six rows of five tiles and an on-screen keyboard whose keys
// show what is known about each letter.
class WordActivity final : public ui::FosActivity {
 public:
  WordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;

 private:
  enum Action : uint16_t { BACK = 1, KEY };

  void load();
  void save() const;
  void submit();
  void updateKeys();

  std::string answer_;
  std::string source_;  // "Daily" or "Pack"
  int day_ = 0;
  std::vector<std::string> guesses_;
  std::string typing_;
  Mark keyMarks_[26] = {};
  std::string message_;
  bool over_ = false;
  bool won_ = false;
};

}  // namespace fos::puzzles
