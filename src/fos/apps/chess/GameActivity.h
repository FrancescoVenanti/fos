#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BoardView.h"
#include "ChessEngine.h"
#include "ChessRules.h"
#include "Lichess.h"
#include "fos/ui/FosActivity.h"

namespace fos::chess {

enum class Mode : uint8_t { Bot, Local, Lichess, Puzzle };

// One chess board screen for every mode:
// - Bot: play the engine (levels 1-5); the game is saved after every move.
// - Local: pass-and-play, the board turns to the side to move, optional clock.
// - Lichess: an ongoing correspondence/classical/rapid game; moves are sent
//   to Lichess at once.
// - Puzzle: the Lichess daily puzzle; the user plays the solution.
// Tap a piece, then a highlighted square. Bot and Local games open on a setup
// panel when there is no game in progress.
class GameActivity final : public ui::FosActivity {
 public:
  GameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode, OnlineGame online = {});

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onTick() override;
  void onBack() override;

 private:
  enum Action : uint16_t {
    BACK = 1,
    SQUARE,   // value = square
    PROMOTE,  // value = piece type
    CANCEL_PROMOTION,
    BUTTON,  // value = index into buttons_
    SETUP_COLOR,
    SETUP_LEVEL,
    SETUP_CLOCK,
    SETUP_FLIP,
    START,
    DIALOG_OK,
    DIALOG_CANCEL,
  };
  enum class Command : uint8_t { None, Undo, Flip, New, Resign, Refresh, Hint, Retry };

  bool load();
  void save() const;
  void startGame();
  bool humanToMove() const;
  bool boardFlipped() const;
  void select(int square);
  void playHuman(const Move& m);
  void afterMove();
  void command(Command cmd);
  void tickClock();
  void updateTexts();

  void drawSetup(ui::Canvas& c, ui::Rect area);
  void drawBar(ui::Canvas& c, const ui::Rect& r, bool whiteSide) const;
  void drawPromotion(ui::Canvas& c) const;

  Mode mode_;
  OnlineGame online_;
  Game game_;
  std::unique_ptr<Engine> engine_;

  bool setup_ = false;
  // Bot settings
  int colorChoice_ = 0;  // 0 white, 1 random, 2 black
  bool humanWhite_ = true;
  int level_ = 2;
  // Local settings
  int clockChoice_ = 0;  // index into CLOCKS
  bool autoFlip_ = true;
  int64_t clockMs_[2] = {0, 0};  // white, black remaining
  unsigned long clockAt_ = 0;    // millis of the last clock update
  bool viewFlipped_ = false;     // manual flip

  int selected_ = -1;
  uint64_t targets_ = 0;
  Move promotion_{};  // pending promotion (from/to set)
  bool choosingPromotion_ = false;
  bool thinking_ = false;  // bot or puzzle reply pending
  unsigned long replyAt_ = 0;
  bool sending_ = false;  // Lichess move pending
  size_t solved_ = 0;     // puzzle: solution moves played
  bool puzzleDone_ = false;
  Command confirm_ = Command::None;

  std::string title_;
  std::string status_;
  std::string message_;        // one-off status (puzzle feedback, send errors), cleared by the next move
  std::string barText_[2][2];  // [white, black][name, info]
  Command buttons_[3] = {};
  const char* buttonLabels_[3] = {};
  int buttonCount_ = 0;
};

}  // namespace fos::chess
