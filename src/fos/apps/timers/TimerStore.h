#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::timers {

// Timers keep absolute wall-clock times (epoch milliseconds) in
// /fos/timers/state.json, so they keep running through deep sleep and
// restarts: a running countdown stores when it ends, not how much is left.
// The ESP32 keeps the system clock through deep sleep; the Sticky also has an
// RTC for power-off.

int64_t nowMs();

enum class Run : uint8_t { Idle = 0, Running, Paused, Done };

// A span of time that counts down: the countdown and each pomodoro phase.
struct Span {
  int64_t durationMs = 0;
  Run run = Run::Idle;
  int64_t endMs = 0;   // when Running
  int64_t leftMs = 0;  // when Paused

  int64_t remaining(int64_t now) const;
  void start(int64_t now);
  void pause(int64_t now);
  void reset();
};

enum class Phase : uint8_t { Work = 0, ShortBreak, LongBreak };

struct Pomodoro {
  uint8_t workMin = 25;
  uint8_t shortMin = 5;
  uint8_t longMin = 15;
  uint8_t rounds = 4;  // work rounds before the long break
  Phase phase = Phase::Work;
  uint8_t round = 1;
  Span span;

  int64_t phaseMs(Phase p) const;
  // The phase after the current one (work -> break -> work ...).
  Phase nextPhase() const;
  // Moves to the next phase, stopped and full length.
  void advance();
  // Applies changed lengths to the current phase when it has not started.
  void syncLength();
};

struct Stopwatch {
  bool running = false;
  int64_t startMs = 0;        // when running: start of the current run
  int64_t accumMs = 0;        // time from earlier runs
  std::vector<int64_t> laps;  // elapsed time at each lap, oldest first

  int64_t elapsed(int64_t now) const;
};

enum class Alert : uint8_t { None = 0, Countdown, Pomodoro };

constexpr size_t MAX_LAPS = 99;

struct State {
  Span countdown;
  Pomodoro pomodoro;
  Stopwatch stopwatch;
  Alert alert = Alert::None;  // a timer ended and was not dismissed
  int64_t alertAtMs = 0;
  uint8_t tab = 0;  // last tab shown in the app
};

// Loaded on first use.
State& state();
bool save();

// Moves timers that have ended to Done and raises the alert (saved). True
// when something ended.
bool update(int64_t now = nowMs());
// Called by every FOs screen's loop: update() at most every 250 ms. True when
// the alert appeared or went away since the last call, so the status bar can
// repaint.
bool poll();
bool alertPending();
// Clears the alert; a finished countdown goes back to idle, a finished
// pomodoro phase moves to the next phase (stopped).
void dismiss();

// Seconds until the next running countdown or pomodoro phase ends, 0 = none.
uint32_t secondsToNextEnd();

// "4:05", "12:30", "1:02:03"; with tenths "12:30.4". Counting down rounds
// up (the second still running); counting up rounds down.
std::string format(int64_t ms, bool tenths = false, bool countUp = false);
const char* phaseName(Phase phase);

}  // namespace fos::timers
