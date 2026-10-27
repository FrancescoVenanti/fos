#include "PuzzlesActivity.h"

#include <ArduinoJson.h>
#include <esp_random.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "fos/apps/flashcards/Deck.h"
#include "fos/core/Json.h"
#include "fos/sync/Sync.h"

namespace fos::puzzles {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char SUDOKU_FILE[] = "/fos/puzzles/sudoku.json";
constexpr char WORD_FILE[] = "/fos/puzzles/word.json";
constexpr const char* LEVELS[] = {"Easy", "Medium", "Hard", "Expert"};
constexpr int CELL_PX = 48;  // not CELL: that is an action of SudokuActivity
constexpr int GRID = CELL_PX * 9;
constexpr int ROW = 84;

// "4,0,0,..." <-> grid.
std::string gridText(const Grid& g) {
  std::string s(81, '0');
  for (int i = 0; i < 81; ++i) s[i] = static_cast<char>('0' + g.cell[i]);
  return s;
}

void gridFrom(const char* text, Grid& g) {
  for (int i = 0; i < 81; ++i) g.cell[i] = text && text[i] >= '0' && text[i] <= '9' ? text[i] - '0' : 0;
}

// Today's answer: the synced daily word when it is for today, else the pack's.
std::string todaysWord(const int day, std::string& source) {
  JsonDocument doc;
  if (loadJson(sync::cachePath(AppId::Puzzles, "cache-daily").c_str(), doc)) {
    std::string word = doc["data"]["answer"] | "";
    const std::string date = doc["data"]["date"] | "";
    for (char& ch : word) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    std::tm t{};
    char today[16] = "";
    if (localNow(t)) snprintf(today, sizeof(today), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    if (word.size() == WORD_LEN && date == today) {
      source = "Daily";
      return word;
    }
  }
  source = "Pack";
  return packWord(day);
}

}  // namespace

// --- menu -----------------------------------------------------------------------

PuzzlesActivity::PuzzlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Puzzles", renderer, mappedInput) {}

void PuzzlesActivity::onEnter() {
  sync::request(AppId::Puzzles);  // the daily word, when online
  reload();
  FosActivity::onEnter();
}

void PuzzlesActivity::reload() {
  JsonDocument doc;
  sudokuSaved_ = loadJson(SUDOKU_FILE, doc) && !(doc["solved"] | false);
  if (sudokuSaved_) {
    const int level = doc["level"] | 0;
    int filled = 0;
    const char* puzzle = doc["puzzle"] | "";
    const char* entries = doc["entries"] | "";
    for (int i = 0; i < 81 && puzzle[i] && entries[i]; ++i) filled += puzzle[i] != '0' || entries[i] != '0';
    char buf[64];
    snprintf(buf, sizeof(buf), "%s in progress, %d of 81 filled", LEVELS[std::clamp(level, 0, 3)], filled);
    sudokuDetail_ = buf;
  } else {
    sudokuDetail_ = "Pick a level to start";
  }
  JsonDocument word;
  const int today = cards::todayNumber();
  if (loadJson(WORD_FILE, word) && (word["day"] | -1) == today) {
    const int n = word["guesses"].size();
    const bool won = word["won"] | false;
    wordDetail_ = won                ? "Solved today in " + std::to_string(n)
                  : n >= MAX_GUESSES ? std::string("Out of guesses today")
                                     : std::to_string(n) + " of 6 guesses used";
  } else {
    wordDetail_ = "Today's word is waiting";
  }
}

void PuzzlesActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Puzzles", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  listRow(c, area.takeTop(ROW), "Word of the day", wordDetail_.c_str(), nullptr, WORD);
  if (sudokuSaved_) listRow(c, area.takeTop(ROW), "Continue Sudoku", sudokuDetail_.c_str(), nullptr, SUDOKU);
  area.takeTop(GAP);
  sectionLabel(c, area.takeTop(32), "NEW SUDOKU");
  area = area.inset(GUTTER, 0);
  const int w = (area.w - GAP) / 2;
  for (int i = 0; i < 4; ++i) {
    button(c, {area.x + (i % 2) * (w + GAP), area.y + (i / 2) * (MIN_TARGET + SHADOW + GAP), w, MIN_TARGET + SHADOW},
           LEVELS[i], NEW_SUDOKU, ButtonStyle::Secondary, static_cast<int16_t>(i));
  }
}

void PuzzlesActivity::onAction(const uint16_t action, const int16_t value) {
  std::unique_ptr<Activity> next;
  switch (action) {
    case BACK:
      onBack();
      return;
    case WORD:
      next = std::make_unique<WordActivity>(renderer, mappedInput);
      break;
    case SUDOKU:
      next = std::make_unique<SudokuActivity>(renderer, mappedInput, -1);
      break;
    case NEW_SUDOKU:
      next = std::make_unique<SudokuActivity>(renderer, mappedInput, value);
      break;
    default:
      return;
  }
  startActivityForResult(std::move(next), [this](const ActivityResult&) { reload(); });
}

// --- Sudoku ---------------------------------------------------------------------

SudokuActivity::SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int level)
    : FosActivity("Sudoku", renderer, mappedInput), level_(level) {}

void SudokuActivity::onEnter() {
  if (level_ < 0 && load()) {
    // continued
  } else {
    level_ = std::clamp(level_, 0, 3);
    generate(static_cast<Level>(level_), esp_random(), puzzle_, solution_);
    entries_ = Grid();
    std::fill(std::begin(notes_), std::end(notes_), 0);
    save();
  }
  updateTexts();
  FosActivity::onEnter();
}

bool SudokuActivity::load() {
  JsonDocument doc;
  if (!loadJson(SUDOKU_FILE, doc) || (doc["solved"] | false)) return false;
  level_ = std::clamp(doc["level"] | 0, 0, 3);
  gridFrom(doc["puzzle"] | "", puzzle_);
  gridFrom(doc["solution"] | "", solution_);
  gridFrom(doc["entries"] | "", entries_);
  JsonArrayConst notes = doc["notes"];
  for (int i = 0; i < 81; ++i) notes_[i] = notes[i] | 0;
  hints_ = doc["hints"] | 0;
  return true;
}

void SudokuActivity::save() const {
  JsonDocument doc;
  doc["level"] = level_;
  doc["puzzle"] = gridText(puzzle_);
  doc["solution"] = gridText(solution_);
  doc["entries"] = gridText(entries_);
  JsonArray notes = doc["notes"].to<JsonArray>();
  for (const uint16_t n : notes_) notes.add(n);
  doc["hints"] = hints_;
  doc["solved"] = solved_;
  saveJson(SUDOKU_FILE, doc);
}

void SudokuActivity::updateTexts() {
  Grid board = puzzle_;
  int filled = 0;
  for (int i = 0; i < 81; ++i) {
    if (!board.cell[i]) board.cell[i] = entries_.cell[i];
    filled += board.cell[i] ? 1 : 0;
  }
  bool right = filled == 81;
  for (int i = 0; i < 81 && right; ++i) right = board.cell[i] == solution_.cell[i];
  solved_ = right;
  char buf[64];
  if (solved_) {
    snprintf(buf, sizeof(buf), "Solved!%s", hints_ ? "" : " No hints used.");
  } else {
    snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %d of 81%s", LEVELS[level_], filled,
             notesMode_ ? "  \xC2\xB7  NOTES" : "");
  }
  status_ = buf;
}

void SudokuActivity::place(const uint8_t digit) {
  if (selected_ < 0 || puzzle_.cell[selected_] || solved_) return;
  if (notesMode_ && digit) {
    if (!entries_.cell[selected_]) notes_[selected_] ^= static_cast<uint16_t>(1u << digit);
  } else {
    entries_.cell[selected_] = entries_.cell[selected_] == digit ? 0 : digit;
    if (digit) {
      // A placed digit clears that pencil mark from its row, column and box.
      const int r = selected_ / 9;
      const int col = selected_ % 9;
      for (int i = 0; i < 81; ++i) {
        if (i / 9 == r || i % 9 == col || (i / 27 == selected_ / 27 && i % 9 / 3 == col / 3)) {
          notes_[i] &= static_cast<uint16_t>(~(1u << digit));
        }
      }
    }
  }
  updateTexts();
  save();
}

void SudokuActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Sudoku", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + 6);
  c.textIn(area.takeTop(30).inset(GUTTER, 0), font::BODY, status_.c_str(), solved_ ? Tone::Black : Tone::Dark,
           Align::Left, true);
  area.takeTop(4);

  Grid board = puzzle_;
  for (int i = 0; i < 81; ++i) {
    if (!board.cell[i]) board.cell[i] = entries_.cell[i];
  }
  const int selDigit = selected_ >= 0 ? board.cell[selected_] : 0;
  const Rect g{(c.width() - GRID) / 2, area.y, GRID, GRID};
  for (int i = 0; i < 81; ++i) {
    const Rect s{g.x + (i % 9) * CELL_PX, g.y + (i / 9) * CELL_PX, CELL_PX, CELL_PX};
    const bool sel = i == selected_;
    const bool same = selDigit && board.cell[i] == selDigit && !sel;
    const bool related = selected_ >= 0 && !sel &&
                         (i / 9 == selected_ / 9 || i % 9 == selected_ % 9 ||
                          (i / 27 == selected_ / 27 && i % 9 / 3 == selected_ % 9 / 3));
    Tone bg = Tone::White;
    if (related) bg = Tone::Light;
    if (same) bg = Tone::Dark;
    if (sel) bg = Tone::Black;
    c.fill(s, bg);
    const Tone ink = sel || same ? Tone::White : Tone::Black;
    if (board.cell[i]) {
      const char d[2] = {static_cast<char>('0' + board.cell[i]), '\0'};
      const bool given = puzzle_.cell[i] != 0;
      const bool clash = !given && conflicts(board, i, board.cell[i]);
      c.textIn(s, font::TITLE, d, ink, Align::Center, true);
      // Player digits are underlined; a clash gets a heavy bar.
      if (!given) c.fill({s.x + 14, s.bottom() - (clash ? 10 : 7), CELL_PX - 28, clash ? 5 : 2}, ink);
    } else if (notes_[i]) {
      const int sub = CELL_PX / 3;
      for (int d = 1; d <= 9; ++d) {
        if (!(notes_[i] & (1u << d))) continue;
        const char t[2] = {static_cast<char>('0' + d), '\0'};
        c.textIn({s.x + (d - 1) % 3 * sub, s.y + (d - 1) / 3 * sub, sub, sub}, font::SMALL, t,
                 sel ? Tone::White : Tone::Dark, Align::Center);
      }
    }
    c.hit(s, CELL, static_cast<int16_t>(i));
  }
  // Thin lines between cells, heavy between boxes.
  for (int k = 1; k < 9; ++k) {
    const int t = k % 3 == 0 ? 3 : 1;
    c.fill({g.x + k * CELL_PX - t / 2, g.y, t, GRID}, Tone::Black);
    c.fill({g.x, g.y + k * CELL_PX - t / 2, GRID, t}, Tone::Black);
  }
  c.frame(g.inset(-BORDER), BORDER, Tone::Black);
  area.takeTop(GRID + BORDER + GAP);

  // Digits 1-9, then Erase, Notes, Hint.
  area = area.inset(GUTTER, 0);
  const int kw = (area.w - 8 * 4) / 9;
  const int kh = 52;
  for (int d = 1; d <= 9; ++d) {
    const char t[2] = {static_cast<char>('0' + d), '\0'};
    button(c, {area.x + (d - 1) * (kw + 4), area.y, kw, kh}, t, DIGIT, ButtonStyle::Secondary, static_cast<int16_t>(d),
           font::TITLE);
  }
  area.takeTop(kh + 8);
  const int bw = (area.w - 2 * GAP) / 3;
  const int bh = std::min(MIN_TARGET, area.bottom() - area.y - 4);
  button(c, {area.x, area.y, bw, bh}, "Erase", ERASE, ButtonStyle::Quiet);
  button(c, {area.x + bw + GAP, area.y, bw, bh}, "Notes", NOTES,
         notesMode_ ? ButtonStyle::Primary : ButtonStyle::Secondary);
  button(c, {area.x + 2 * (bw + GAP), area.y, bw, bh}, "Hint", HINT, ButtonStyle::Secondary);
}

void SudokuActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case CELL:
      selected_ = value;
      return;
    case DIGIT:
      place(static_cast<uint8_t>(value));
      return;
    case ERASE:
      if (selected_ >= 0 && !puzzle_.cell[selected_]) {
        entries_.cell[selected_] = 0;
        notes_[selected_] = 0;
        updateTexts();
        save();
      }
      return;
    case NOTES:
      notesMode_ = !notesMode_;
      updateTexts();
      return;
    case HINT:
      // Fills the selected cell, or the first empty or wrong one.
      if (solved_) return;
      if (selected_ < 0 || puzzle_.cell[selected_] || entries_.cell[selected_] == solution_.cell[selected_]) {
        for (int i = 0; i < 81; ++i) {
          if (!puzzle_.cell[i] && entries_.cell[i] != solution_.cell[i]) {
            selected_ = i;
            break;
          }
        }
      }
      if (selected_ >= 0 && !puzzle_.cell[selected_]) {
        entries_.cell[selected_] = solution_.cell[selected_];
        notes_[selected_] = 0;
        ++hints_;
        updateTexts();
        save();
      }
      return;
    default:
      return;
  }
}

// Up/Down move the selection through the cells.
bool SudokuActivity::onUp() {
  selected_ = selected_ <= 0 ? 80 : selected_ - 1;
  return true;
}

bool SudokuActivity::onDown() {
  selected_ = selected_ < 0 || selected_ >= 80 ? 0 : selected_ + 1;
  return true;
}

// --- word puzzle ----------------------------------------------------------------

namespace {

constexpr const char* KEY_ROWS[3] = {"QWERTYUIOP", "ASDFGHJKL", "\x01ZXCVBNM\x02"};  // 1 = Enter, 2 = Backspace
constexpr int TILE = 64;

}  // namespace

WordActivity::WordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("WordPuzzle", renderer, mappedInput) {}

void WordActivity::onEnter() {
  load();
  updateKeys();
  FosActivity::onEnter();
}

void WordActivity::load() {
  day_ = cards::todayNumber();
  answer_ = todaysWord(day_, source_);
  guesses_.clear();
  JsonDocument doc;
  // Today's game continues; a new day (or a different daily word) starts over.
  if (loadJson(WORD_FILE, doc) && (doc["day"] | -1) == day_ && answer_ == (doc["answer"] | "")) {
    for (JsonVariantConst g : doc["guesses"].as<JsonArrayConst>()) guesses_.emplace_back(g | "");
  }
  won_ = !guesses_.empty() && guesses_.back() == answer_;
  over_ = won_ || static_cast<int>(guesses_.size()) >= MAX_GUESSES;
  if (won_) message_ = "Solved in " + std::to_string(guesses_.size()) + "!";
  if (over_ && !won_) message_ = "The word was " + answer_;
}

void WordActivity::save() const {
  JsonDocument doc;
  doc["day"] = day_;
  doc["answer"] = answer_;
  JsonArray g = doc["guesses"].to<JsonArray>();
  for (const std::string& s : guesses_) g.add(s);
  doc["won"] = won_;
  saveJson(WORD_FILE, doc);
}

void WordActivity::updateKeys() {
  std::fill(std::begin(keyMarks_), std::end(keyMarks_), Mark::Unknown);
  for (const std::string& g : guesses_) {
    Mark m[WORD_LEN];
    score(g, answer_, m);
    for (int i = 0; i < WORD_LEN; ++i) {
      Mark& k = keyMarks_[g[i] - 'A'];
      if (static_cast<int>(m[i]) > static_cast<int>(k)) k = m[i];
    }
  }
}

void WordActivity::submit() {
  if (typing_.size() != WORD_LEN) {
    message_ = "Five letters, please.";
    return;
  }
  if (!isWord(typing_) && typing_ != answer_) {
    message_ = "Not in the word list.";
    return;
  }
  guesses_.push_back(typing_);
  typing_.clear();
  message_.clear();
  won_ = guesses_.back() == answer_;
  over_ = won_ || static_cast<int>(guesses_.size()) >= MAX_GUESSES;
  if (won_) message_ = "Solved in " + std::to_string(guesses_.size()) + "!";
  if (over_ && !won_) message_ = "The word was " + answer_;
  updateKeys();
  save();
  refresh(true);
}

void WordActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), source_ == "Daily" ? "Daily word" : "Word of the day", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);

  const int gap = 6;
  const int gridW = WORD_LEN * TILE + (WORD_LEN - 1) * gap;
  const int x0 = (c.width() - gridW) / 2;
  for (int r = 0; r < MAX_GUESSES; ++r) {
    std::string word;
    Mark marks[WORD_LEN] = {};
    if (r < static_cast<int>(guesses_.size())) {
      word = guesses_[r];
      score(word, answer_, marks);
    } else if (r == static_cast<int>(guesses_.size()) && !over_) {
      word = typing_;
    }
    for (int i = 0; i < WORD_LEN; ++i) {
      const Rect t{x0 + i * (TILE + gap), area.y + r * (TILE + gap), TILE, TILE};
      Tone bg = Tone::White;
      Tone ink = Tone::Black;
      if (marks[i] == Mark::Correct) bg = Tone::Black, ink = Tone::White;
      if (marks[i] == Mark::Present) bg = Tone::Dark, ink = Tone::White;
      if (marks[i] == Mark::Absent) bg = Tone::Light;
      c.fill(t, bg);
      c.frame(t, BORDER, Tone::Black);
      // Present letters get a corner notch too, so they read without gray.
      if (marks[i] == Mark::Present) c.fill({t.right() - 14, t.y + BORDER, 11, 11}, Tone::White);
      if (i < static_cast<int>(word.size())) {
        const char ch[2] = {word[i], '\0'};
        c.textIn(t, font::LARGE, ch, ink, Align::Center, true);
      }
    }
  }
  area.takeTop(MAX_GUESSES * (TILE + gap) + 4);
  c.textIn(area.takeTop(34), font::BODY, message_.c_str(), Tone::Black, Align::Center, true);

  // Keyboard.
  const Rect kb = area.inset(6, 0);
  const int kh = std::min(58, (kb.h - 2 * 6) / 3);
  const int kw = (kb.w - 9 * 4) / 10;
  for (int r = 0; r < 3; ++r) {
    const char* keys = KEY_ROWS[r];
    const int n = static_cast<int>(strlen(keys));
    int total = 0;
    for (int i = 0; i < n; ++i) total += keys[i] < 'A' ? kw * 3 / 2 : kw;
    total += (n - 1) * 4;
    int x = kb.x + (kb.w - total) / 2;
    const int y = kb.y + r * (kh + 6);
    for (int i = 0; i < n; ++i) {
      const char k = keys[i];
      const int w = k < 'A' ? kw * 3 / 2 : kw;
      const Rect key{x, y, w, kh};
      if (k == '\x01') {
        button(c, key, "OK", KEY, ButtonStyle::Primary, 1, font::SMALL);
      } else if (k == '\x02') {
        glyphButton(c, key, Glyph::Backspace, KEY, ButtonStyle::Quiet, 2);
      } else {
        const Mark m = keyMarks_[k - 'A'];
        const bool down = c.pressed(KEY, k);
        Tone bg = m == Mark::Correct   ? Tone::Black
                  : m == Mark::Present ? Tone::Dark
                  : m == Mark::Absent  ? Tone::White
                                       : Tone::Light;
        if (down) bg = Tone::Black;
        c.fill(key, bg);
        c.frame(key, 2, Tone::Black);
        const char t[2] = {k, '\0'};
        const bool light = bg == Tone::Black || bg == Tone::Dark;
        c.textIn(key, font::BODY, t, light ? Tone::White : (m == Mark::Absent ? Tone::Dark : Tone::Black),
                 Align::Center, true);
        c.hit(key, KEY, k);
      }
      x += w + 4;
    }
  }
}

void WordActivity::onAction(const uint16_t action, const int16_t value) {
  if (action == BACK) {
    onBack();
    return;
  }
  if (action != KEY || over_) return;
  if (value == 1) {
    submit();
  } else if (value == 2) {
    if (!typing_.empty()) typing_.pop_back();
    message_.clear();
  } else if (value >= 'A' && value <= 'Z' && typing_.size() < WORD_LEN) {
    typing_ += static_cast<char>(value);
    message_.clear();
  }
}

}  // namespace fos::puzzles
