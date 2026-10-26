#pragma once

#include <string>

#include "TimerStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::timers {

// Timers (docs/PRODUCT.md): Pomodoro, stopwatch with laps, countdown. All
// three run at once and keep running through sleep; an ended timer shows
// "TIME'S UP" here, in the status bar and on the lock screen until dismissed.
// Up/Down switch tabs.
class TimersActivity final : public ui::FosActivity {
 public:
  TimersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum class Tab : uint8_t { Pomodoro = 0, Stopwatch, Countdown };

  enum Action : uint16_t {
    BACK = 1,
    TAB,
    PRIMARY,    // start / pause / resume / dismiss
    SECONDARY,  // reset / lap / skip
    PRESET,     // value = index in PRESETS
    ADJUST,     // value = seconds to add (may be negative)
    POMO_STEP,  // value = row * 2 + (0 = minus, 1 = plus)
  };

  Tab tab() const { return static_cast<Tab>(state().tab); }
  // Rebuilds the texts drawn this frame (never built while drawing).
  void updateTexts(int64_t now);
  const Span* activeSpan() const;
  bool running() const;

  void drawDisplay(ui::Canvas& c, const ui::Rect& r) const;
  void drawCountdown(ui::Canvas& c, ui::Rect area);
  void drawStopwatch(ui::Canvas& c, ui::Rect area) const;
  void drawPomodoro(ui::Canvas& c, ui::Rect area) const;
  void drawActions(ui::Canvas& c, const ui::Rect& r) const;

  int64_t shownKey_ = -1;  // second the texts were built for
  std::string big_;        // main figure
  std::string label_;      // above the figure
  std::string note_;       // below the figure
  bool inverted_ = false;  // running or done: white on black
  bool done_ = false;
  const char* primary_ = "";
  const char* secondary_ = nullptr;
  std::string lapRows_[5][3];
  int lapCount_ = 0;
  std::string pomoValues_[4];
};

}  // namespace fos::timers
