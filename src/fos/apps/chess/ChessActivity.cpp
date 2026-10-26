#include "ChessActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>

#include "BoardView.h"
#include "GameActivity.h"
#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/sync/Backend.h"

namespace fos::chess {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char TRAINER_FILE[] = "/fos/chess/trainer.json";
constexpr int DAILY_GOAL_S = 15 * 60;
constexpr int MAX_COUNTED_S = 20;  // per answer, so an idle screen does not count
constexpr const char* DRILLS[] = {"Square color", "Find the square"};
constexpr int ROW = 84;

constexpr const char* MODES[] = {"Vs bot", "1v1", "Lichess", "Blindfold trainer"};

// "Game in progress, move 12" from a saved game file.
std::string savedGame(const char* path, const char* fresh) {
  JsonDocument doc;
  if (!loadJson(path, doc)) return fresh;
  Game g;
  g.playUciList(doc["moves"] | "");
  if ((doc["over"] | false) || g.over()) return "Last game finished. Tap for a new one";
  if (g.moves().empty()) return "New game ready";
  return "Game in progress, move " + std::to_string(g.position().fullmove);
}

std::string dayStamp(const int offsetDays) {
  std::tm t{};
  if (!localNow(t)) return {};
  t.tm_mday += offsetDays;
  t.tm_hour = 12;
  std::mktime(&t);
  char out[16];
  snprintf(out, sizeof(out), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return out;
}

// Trainer stats as stored, with the streak already broken if a day was missed.
void loadTrainer(int& streak, std::string& lastDay, std::string& today, int& todaySeconds) {
  JsonDocument doc;
  loadJson(TRAINER_FILE, doc);
  streak = doc["streak"] | 0;
  lastDay = doc["lastDay"] | "";
  today = doc["today"] | "";
  todaySeconds = doc["todaySeconds"] | 0;
  const std::string now = dayStamp(0);
  if (today != now) {
    today = now;
    todaySeconds = 0;
  }
  if (!lastDay.empty() && lastDay != now && lastDay != dayStamp(-1)) streak = 0;
}

std::string trainerSummary() {
  int streak = 0;
  int seconds = 0;
  std::string last;
  std::string today;
  loadTrainer(streak, last, today, seconds);
  char out[64];
  snprintf(out, sizeof(out), "Streak %d %s  \xC2\xB7  %d of 15 min today", streak, streak == 1 ? "day" : "days",
           std::min(15, seconds / 60));
  return out;
}

}  // namespace

// --- Chess home ---------------------------------------------------------------

ChessActivity::ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Chess", renderer, mappedInput) {}

void ChessActivity::onEnter() {
  reload();
  FosActivity::onEnter();
}

void ChessActivity::reload() {
  details_[0] = savedGame("/fos/chess/bot.json", "Play the engine, 5 levels, offline");
  details_[1] = savedGame("/fos/chess/local.json", "Pass and play, optional clock");
  details_[2] = lichess::hasToken() ? "Your games and the daily puzzle" : "Daily puzzle. Add a token for your games";
  details_[3] = trainerSummary();
}

void ChessActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Chess", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  for (int i = 0; i < 4; ++i) {
    const Rect r = area.takeTop(ROW);
    listRow(c, r, MODES[i], details_[i].c_str(), nullptr, OPEN, static_cast<int16_t>(i));
  }
  // A small board preview fills the rest: the starting position, pieces only.
  area.takeTop(GAP * 2);
  Position start;
  start.setStart();
  const int size = 32;
  const int w = 8 * size;
  const int x0 = (c.width() - w) / 2;
  if (area.h >= 2 * size + GAP) {
    for (int file = 0; file < 8; ++file) {
      drawPiece(c, x0 + file * size, area.y, pieceType(start.sq[56 + file]), false, size);
      drawPiece(c, x0 + file * size, area.y + size + 4, PAWN, false, size);
    }
  }
}

void ChessActivity::onAction(const uint16_t action, const int16_t value) {
  if (action == BACK) {
    onBack();
    return;
  }
  if (action != OPEN) return;
  std::unique_ptr<Activity> next;
  switch (value) {
    case 0:
      next = std::make_unique<GameActivity>(renderer, mappedInput, Mode::Bot);
      break;
    case 1:
      next = std::make_unique<GameActivity>(renderer, mappedInput, Mode::Local);
      break;
    case 2:
      next = std::make_unique<LichessActivity>(renderer, mappedInput);
      break;
    default:
      next = std::make_unique<TrainerActivity>(renderer, mappedInput);
      break;
  }
  startActivityForResult(std::move(next), [this](const ActivityResult&) { reload(); });
}

// --- Lichess --------------------------------------------------------------------

LichessActivity::LichessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Lichess", renderer, mappedInput) {}

void LichessActivity::onEnter() {
  wifiWasOn_ = WiFi.status() == WL_CONNECTED;
  if (lichess::hasToken()) startLoad(Load::Games);
  FosActivity::onEnter();
}

void LichessActivity::onExit() {
  // Lichess is the only thing here that wanted Wi-Fi.
  if (!wifiWasOn_) net::wifiOff();
  FosActivity::onExit();
}

void LichessActivity::startLoad(const Load what) {
  pending_ = what;
  loading_ = true;
  error_.clear();
  loadAt_ = millis() + 200;  // the "Loading" frame goes out first
}

void LichessActivity::onTick() {
  if (!loading_ || millis() < loadAt_) return;
  loading_ = false;
  const Load what = pending_;
  pending_ = Load::None;
  if (what == Load::Games) {
    std::string error;
    if (!lichess::fetchGames(games_, hiddenFast_, error)) error_ = error;
    details_.clear();
    for (const OnlineGame& g : games_) {
      std::string speed = g.speed;
      if (!speed.empty()) speed[0] = static_cast<char>(toupper(static_cast<unsigned char>(speed[0])));
      details_.push_back(speed + "  \xC2\xB7  " + (g.white ? "White" : "Black") +
                         (g.myTurn ? "  \xC2\xB7  your move" : "  \xC2\xB7  waiting"));
    }
    note_ = hiddenFast_ > 0 ? std::to_string(hiddenFast_) + " faster game" + (hiddenFast_ == 1 ? "" : "s") +
                                  " not shown: blitz does not suit e-paper."
                            : "";
    refresh(true);
    return;
  }
  OnlineGame puzzle;
  std::string error;
  if (!lichess::fetchPuzzle(puzzle, error)) {
    error_ = error;
    refresh(true);
    return;
  }
  startActivityForResult(std::make_unique<GameActivity>(renderer, mappedInput, Mode::Puzzle, std::move(puzzle)),
                         [](const ActivityResult&) {});
}

void LichessActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Lichess", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);

  listRow(c, area.takeTop(ROW), "Daily puzzle", "Find the winning line", nullptr, PUZZLE);
  sectionLabel(c, area.takeTop(40), "YOUR GAMES");
  rule(c, 0, area.y - BORDER, c.width());

  if (!lichess::hasToken()) {
    emptyState(c, area, "No account",
               "Create a personal API token on lichess.org (scope: play games with the board API) and enter it in "
               "Settings > Accounts.");
    return;
  }
  if (loading_) {
    emptyState(c, area, "Loading...", "Asking Lichess for your games.");
    return;
  }
  if (!error_.empty()) {
    const Rect retry = area.takeBottom(MIN_TARGET + SHADOW + GUTTER).inset(GUTTER, 0);
    button(c, {retry.x, retry.y, retry.w, MIN_TARGET + SHADOW}, "Try again", RETRY);
    emptyState(c, area, "Lichess unavailable", error_.c_str());
    return;
  }
  if (games_.empty()) {
    emptyState(c, area, "No games", "Start a correspondence, classical or rapid game on lichess.org.");
    return;
  }
  for (size_t i = 0; i < games_.size() && area.h >= ROW + 40; ++i) {
    const std::string title = "vs " + games_[i].opponent;
    listRow(c, area.takeTop(ROW), title.c_str(), details_[i].c_str(), nullptr, GAME, static_cast<int16_t>(i));
  }
  if (!note_.empty()) c.textWrapped(area.inset(GUTTER, 8), font::SMALL, note_.c_str(), Tone::Dark, 2);
}

void LichessActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case PUZZLE:
      startLoad(Load::Puzzle);
      return;
    case RETRY:
      startLoad(Load::Games);
      return;
    case GAME:
      if (value >= 0 && value < static_cast<int>(games_.size())) {
        startActivityForResult(std::make_unique<GameActivity>(renderer, mappedInput, Mode::Lichess, games_[value]),
                               [this](const ActivityResult&) { startLoad(Load::Games); });
      }
      return;
    default:
      return;
  }
}

// --- Blindfold trainer ----------------------------------------------------------

TrainerActivity::TrainerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("ChessTrainer", renderer, mappedInput) {}

void TrainerActivity::onEnter() {
  loadTrainer(streak_, lastDay_, today_, todaySeconds_);
  nextQuestion();
  FosActivity::onEnter();
}

void TrainerActivity::onExit() {
  saveStats();
  FosActivity::onExit();
}

void TrainerActivity::saveStats() const {
  JsonDocument doc;
  doc["streak"] = streak_;
  doc["lastDay"] = lastDay_;
  doc["today"] = today_;
  doc["todaySeconds"] = todaySeconds_;
  saveJson(TRAINER_FILE, doc);
}

void TrainerActivity::nextQuestion() {
  int next = static_cast<int>(esp_random() % 64);
  if (next == square_) next = (next + 17) % 64;
  square_ = next;
  askedAt_ = millis();
  updateTexts();
}

void TrainerActivity::answered(const bool right) {
  (right ? right_ : wrong_)++;
  const int seconds = static_cast<int>(std::min<unsigned long>(MAX_COUNTED_S, (millis() - askedAt_ + 999) / 1000));
  const bool wasDone = todaySeconds_ >= DAILY_GOAL_S;
  todaySeconds_ += seconds;
  if (!wasDone && todaySeconds_ >= DAILY_GOAL_S && lastDay_ != today_) {
    // Goal reached today: the streak goes on if yesterday counted too.
    streak_ = lastDay_ == dayStamp(-1) ? streak_ + 1 : 1;
    lastDay_ = today_;
  }
  char name[3];
  squareName(square_, name);
  if (drill_ == 0) {
    const bool light = (fileOf(square_) + rankOf(square_)) % 2 == 1;
    feedback_ = std::string(right ? "Right: " : "No: ") + name + " is " + (light ? "light." : "dark.");
  } else {
    feedback_ = std::string(right ? "Right: " : "No: ") + name + " is the black square.";
  }
  previous_ = square_;
  saveStats();
  nextQuestion();
}

void TrainerActivity::updateTexts() {
  char name[3];
  squareName(square_, name);
  prompt_ = drill_ == 0 ? std::string(name) : std::string("Tap ") + name;
  char buf[64];
  snprintf(buf, sizeof(buf), "%d right  \xC2\xB7  %d wrong", right_, wrong_);
  score_ = buf;
  snprintf(buf, sizeof(buf), "%d %s", streak_, streak_ == 1 ? "day" : "days");
  streakText_ = buf;
  if (todaySeconds_ >= DAILY_GOAL_S) {
    todayText_ = "Done";
  } else {
    snprintf(buf, sizeof(buf), "%d / 15 min", todaySeconds_ / 60);
    todayText_ = buf;
  }
}

void TrainerActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Blindfold trainer", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + 8);
  Rect inner = area.inset(GUTTER, 0);

  // Stats: streak, today's time with a progress bar.
  Rect stats = inner.takeTop(64);
  const int half = (stats.w - GAP) / 2;
  const Rect left = stats.takeLeft(half);
  stats.takeLeft(GAP);
  c.textIn({left.x, left.y, left.w, 22}, font::SMALL, "STREAK", Tone::Dark, Align::Left, true);
  c.textIn({left.x, left.y + 22, left.w, 36}, font::TITLE, streakText_.c_str(), Tone::Black, Align::Left, true);
  c.textIn({stats.x, stats.y, stats.w, 22}, font::SMALL, "TODAY", Tone::Dark, Align::Left, true);
  c.textIn({stats.x, stats.y + 22, stats.w, 22}, font::BODY, todayText_.c_str(), Tone::Black, Align::Left, true);
  progressBar(c, {stats.x, stats.y + 48, stats.w, 14}, std::min(todaySeconds_, DAILY_GOAL_S), DAILY_GOAL_S,
              Tone::Black);
  inner.takeTop(GAP);

  segmented(c, inner.takeTop(TAB_H), DRILLS, 2, drill_, DRILL);
  inner.takeTop(GAP);

  if (drill_ == 0) {
    // The square's name, big, then Light / Dark.
    const Rect card = inner.takeTop(250);
    const Rect face = block(c, card, Tone::White);
    const int scale = 4 * 256;
    const int w = c.scaledWidth(font::LARGE, prompt_.c_str(), scale);
    const int baseline = face.y + face.h / 2 + c.ascender(font::LARGE) * scale / 256 / 2;
    c.scaledText(font::LARGE, face.x + (face.w - w) / 2, baseline, prompt_.c_str(), scale);
    inner.takeTop(GAP * 2);
    const Rect answers = inner.takeTop(110);
    const int bw = (answers.w - GAP) / 2;
    const Rect lightFace = block(c, {answers.x, answers.y, bw, answers.h}, Tone::White, ANSWER, 1);
    c.textIn(lightFace, font::TITLE, "Light", Tone::Black, Align::Center, true);
    const Rect darkFace = block(c, {answers.x + bw + GAP, answers.y, bw, answers.h}, Tone::Light, ANSWER, 0);
    c.textIn(darkFace, font::TITLE, "Dark", Tone::Black, Align::Center, true);
    inner.takeTop(GAP * 2);
    c.textIn(inner.takeTop(c.lineHeight(font::BODY)), font::BODY, feedback_.c_str(), Tone::Black, Align::Center, true);
    c.textIn(inner.takeTop(c.lineHeight(font::BODY)), font::BODY, score_.c_str(), Tone::Dark, Align::Center);
    return;
  }

  // Find the square: a bare board without coordinates.
  const Rect prompt = inner.takeTop(40);
  c.textIn(prompt, font::TITLE, prompt_.c_str(), Tone::Black, Align::Left, true);
  c.textIn(prompt, font::SMALL, score_.c_str(), Tone::Dark, Align::Right);
  inner.takeTop(BORDER + 2);
  Position empty;
  BoardStyle style;
  style.showPieces = false;
  style.coordinates = false;
  style.marked = previous_;
  drawBoard(c, {(c.width() - BOARD) / 2, inner.y, BOARD, BOARD}, empty, style, SQUARE);
  inner.takeTop(BOARD + BORDER + 6);
  c.textIn(inner.takeTop(c.lineHeight(font::BODY)), font::BODY, feedback_.c_str(), Tone::Black, Align::Center, true);
}

void TrainerActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case DRILL:
      drill_ = value;
      feedback_.clear();
      previous_ = -1;
      nextQuestion();
      return;
    case ANSWER: {
      const bool light = (fileOf(square_) + rankOf(square_)) % 2 == 1;
      answered((value == 1) == light);
      return;
    }
    case SQUARE:
      if (drill_ == 1) answered(value == square_);
      return;
    default:
      return;
  }
}

bool TrainerActivity::onUp() {
  drill_ = 0;
  feedback_.clear();
  nextQuestion();
  return true;
}

bool TrainerActivity::onDown() {
  drill_ = 1;
  feedback_.clear();
  previous_ = -1;
  nextQuestion();
  return true;
}

}  // namespace fos::chess
