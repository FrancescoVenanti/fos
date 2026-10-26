#pragma once

#include <string>
#include <vector>

#include "Lichess.h"
#include "fos/ui/FosActivity.h"

namespace fos::chess {

// Chess home: the four modes (docs/PRODUCT.md), each with its state.
class ChessActivity final : public ui::FosActivity {
 public:
  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;

 private:
  enum Action : uint16_t { BACK = 1, OPEN };
  void reload();

  std::string details_[4];
};

// Lichess: the daily puzzle and the user's ongoing games (no blitz).
class LichessActivity final : public ui::FosActivity {
 public:
  LichessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onTick() override;

 private:
  enum Action : uint16_t { BACK = 1, PUZZLE, GAME, RETRY };
  enum class Load : uint8_t { None, Games, Puzzle };

  void startLoad(Load what);

  Load pending_ = Load::None;
  unsigned long loadAt_ = 0;
  bool loading_ = false;
  bool wifiWasOn_ = false;
  std::vector<OnlineGame> games_;
  std::vector<std::string> details_;
  int hiddenFast_ = 0;
  std::string error_;
  std::string note_;
};

// Blindfold trainer: square colors and finding squares on a bare board, a
// 15-minute daily goal and a streak of days that reached it.
class TrainerActivity final : public ui::FosActivity {
 public:
  TrainerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;

 private:
  enum Action : uint16_t { BACK = 1, DRILL, ANSWER, SQUARE };

  void nextQuestion();
  void answered(bool right);
  void updateTexts();
  void saveStats() const;

  int drill_ = 0;  // 0 = square color, 1 = find the square
  int square_ = 0;
  int previous_ = -1;  // find drill: last asked square, shown on the board
  int right_ = 0;
  int wrong_ = 0;
  unsigned long askedAt_ = 0;

  int streak_ = 0;
  std::string lastDay_;  // last day the goal was reached
  std::string today_;
  int todaySeconds_ = 0;

  std::string prompt_;
  std::string feedback_;
  std::string score_;
  std::string streakText_;
  std::string todayText_;
};

}  // namespace fos::chess
