#include "CalculatorActivity.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CalcEngine.h"
#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/sync/Sync.h"

namespace fos::calc {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char STATE_FILE[] = "/fos/calculator/state.json";
constexpr const char* TABS[] = {"Calculator", "Convert"};
constexpr const char* CATEGORIES[] = {"Length", "Weight", "Volume", "Temp", "Money"};
// The pair each category opens on: m to ft, kg to lb, l to gal, C to F, EUR to the first rate.
constexpr int DEFAULT_PAIR[5][2] = {{2, 5}, {1, 4}, {1, 7}, {0, 1}, {0, 1}};
constexpr int CATEGORY_COUNT = 5;
constexpr int CURRENCY = 4;
constexpr int TEMPERATURE = 3;

// Calculator keys, row by row.
enum Key : int8_t { K_CLEAR = 10, K_OPEN, K_CLOSE, K_DIV, K_MUL, K_SUB, K_ADD, K_EQ, K_DOT, K_NEG, K_BACK, K_PCT };
constexpr int8_t KEYS[] = {K_CLEAR, K_OPEN, K_CLOSE, K_DIV, 7, 8,     9,     K_MUL, 4,     5,
                           6,       K_SUB,  1,       2,     3, K_ADD, K_NEG, 0,     K_DOT, K_EQ};
constexpr int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

struct UnitDef {
  const char* name;
  double factor;
};
constexpr UnitDef LENGTH[] = {{"mm", 0.001},  {"cm", 0.01},   {"m", 1},       {"km", 1000},
                              {"in", 0.0254}, {"ft", 0.3048}, {"yd", 0.9144}, {"mi", 1609.344}};
constexpr UnitDef WEIGHT[] = {{"g", 0.001},           {"kg", 1},          {"t", 1000},
                              {"oz", 0.028349523125}, {"lb", 0.45359237}, {"st", 6.35029318}};
constexpr UnitDef VOLUME[] = {{"ml", 0.001},          {"l", 1},
                              {"tsp", 0.00492892159}, {"tbsp", 0.0147867648},
                              {"cup", 0.2365882365},  {"fl oz", 0.0295735296},
                              {"pt", 0.473176473},    {"gal", 3.785411784}};
constexpr UnitDef TEMPS[] = {{"\xC2\xB0"
                              "C",
                              0},
                             {"\xC2\xB0"
                              "F",
                              0},
                             {"K", 0}};

bool jumboSafe(const std::string& text) {
  // The jumbo font has digits, + - . , % : and the minus and euro signs only.
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch >= 0x80) {
      if (text.compare(i, 3, "\xE2\x88\x92") == 0 || text.compare(i, 3, "\xE2\x82\xAC") == 0) {
        i += 2;
        continue;
      }
      return false;
    }
    if (!(isdigit(ch) || strchr(" %+,-./:", ch))) return false;
  }
  return true;
}

// Right-aligned big figure that falls back to smaller fonts when it does not fit.
void figure(Canvas& c, const Rect& r, const std::string& text, const Tone tone) {
  int fontId = jumboSafe(text) ? font::JUMBO : font::LARGE;
  if (c.textWidth(fontId, text.c_str(), true) > r.w) fontId = font::LARGE;
  if (c.textWidth(fontId, text.c_str(), true) > r.w) fontId = font::TITLE;
  c.textIn(r, fontId, text.c_str(), tone, Align::Right, true);
}

const char* keyLabel(const int8_t k) {
  static const char* DIGITS[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  if (k >= 0 && k <= 9) return DIGITS[k];
  switch (k) {
    case K_CLEAR:
      return "C";
    case K_OPEN:
      return "(";
    case K_CLOSE:
      return ")";
    case K_DIV:
      return "\xC3\xB7";
    case K_MUL:
      return "\xC3\x97";
    case K_SUB:
      return "\xE2\x88\x92";
    case K_ADD:
      return "+";
    case K_EQ:
      return "=";
    case K_NEG:
      return "\xC2\xB1";
    default:
      return "";
  }
}

bool isOperator(const char ch) { return ch == '+' || ch == '-' || ch == '*' || ch == '/'; }

}  // namespace

CalculatorActivity::CalculatorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Calculator", renderer, mappedInput), keypad_({}, 6, 12) {}

void CalculatorActivity::onEnter() {
  JsonDocument doc;
  if (loadJson(STATE_FILE, doc)) {
    tab_ = doc["tab"] | 0;
    expr_ = doc["expr"] | "";
    category_ = doc["category"] | 0;
    from_ = doc["from"] | 0;
    to_ = doc["to"] | 1;
    keypad_.setText(doc["value"] | "1");
    negative_ = doc["negative"] | false;
  } else {
    keypad_.setText("1");
    from_ = DEFAULT_PAIR[0][0];
    to_ = DEFAULT_PAIR[0][1];
  }
  if (category_ < 0 || category_ >= CATEGORY_COUNT) category_ = 0;
  seenGen_ = sync::generation();
  sync::request(AppId::Calculator);  // fresh rates when online
  loadUnits();
  updateCalc();
  updateConvert();
  FosActivity::onEnter();
}

void CalculatorActivity::onExit() {
  JsonDocument doc;
  doc["tab"] = tab_;
  doc["expr"] = expr_;
  doc["category"] = category_;
  doc["from"] = from_;
  doc["to"] = to_;
  doc["value"] = keypad_.text();
  doc["negative"] = negative_;
  saveJson(STATE_FILE, doc);
  FosActivity::onExit();
}

// --- calculator -----------------------------------------------------------------

void CalculatorActivity::press(const int key) {
  error_ = false;
  if (justEvaluated_) {
    // A digit starts over; an operator continues from the answer.
    if ((key >= 0 && key <= 9) || key == K_DOT || key == K_OPEN) expr_.clear();
    justEvaluated_ = false;
  }
  const char last = expr_.empty() ? '\0' : expr_.back();
  if (key >= 0 && key <= 9) {
    if (last == ')' || last == '%') expr_ += '*';
    expr_ += static_cast<char>('0' + key);
  } else if (key == K_DOT) {
    // One dot per number.
    size_t i = expr_.size();
    while (i > 0 && (isdigit(static_cast<unsigned char>(expr_[i - 1])) || expr_[i - 1] == '.')) --i;
    if (expr_.find('.', i) == std::string::npos) expr_ += i == expr_.size() ? "0." : ".";
  } else if (key == K_ADD || key == K_SUB || key == K_MUL || key == K_DIV) {
    const char op = key == K_ADD ? '+' : key == K_SUB ? '-' : key == K_MUL ? '*' : '/';
    if (expr_.empty() || last == '(') {
      if (op == '-') expr_ += '-';  // a leading minus
    } else if (isOperator(last)) {
      expr_.back() = op;
    } else {
      expr_ += op;
    }
  } else if (key == K_OPEN) {
    if (!expr_.empty() && (isdigit(static_cast<unsigned char>(last)) || last == ')' || last == '%')) expr_ += '*';
    expr_ += '(';
  } else if (key == K_CLOSE) {
    int open = 0;
    for (const char ch : expr_) open += ch == '(' ? 1 : ch == ')' ? -1 : 0;
    if (open > 0 && !isOperator(last) && last != '(') expr_ += ')';
  } else if (key == K_PCT) {
    if (isdigit(static_cast<unsigned char>(last)) || last == ')') expr_ += '%';
  } else if (key == K_NEG) {
    // Negates the last number: "12+3" -> "12+(-3)".
    size_t i = expr_.size();
    while (i > 0 && (isdigit(static_cast<unsigned char>(expr_[i - 1])) || expr_[i - 1] == '.')) --i;
    if (i < expr_.size()) {
      if (i >= 2 && expr_.compare(i - 2, 2, "(-") == 0) {
        expr_.erase(i - 2, 2);
      } else {
        expr_.insert(i, "(-");
        expr_ += ')';
      }
    } else if (expr_.size() >= 3 && expr_.back() == ')') {
      // Already "(-n)": undo.
      const size_t open = expr_.rfind("(-");
      if (open != std::string::npos) {
        expr_.erase(expr_.size() - 1);
        expr_.erase(open, 2);
      }
    }
  } else if (key == K_BACK) {
    if (!expr_.empty()) expr_.pop_back();
  } else if (key == K_CLEAR) {
    expr_.clear();
  } else if (key == K_EQ) {
    double v = 0;
    if (calc::evaluate(expr_, v)) {
      shownExpr_ = calc::displayExpression(expr_) + " =";
      char buf[40];
      snprintf(buf, sizeof(buf), "%.12g", v);
      expr_ = buf;
      if (expr_.find('e') != std::string::npos) expr_.clear();  // too big to continue from
      result_ = calc::formatNumber(v);
      justEvaluated_ = true;
      return;
    }
    error_ = true;
  }
  updateCalc();
}

void CalculatorActivity::updateCalc() {
  if (justEvaluated_) return;
  shownExpr_ = expr_.empty() ? std::string() : calc::displayExpression(expr_);
  double v = 0;
  if (error_) {
    result_ = "Error";
  } else if (expr_.empty()) {
    result_ = "0";
  } else if (calc::evaluate(expr_, v)) {
    result_ = calc::formatNumber(v);
  }
  // An incomplete expression ("12+") keeps the last good preview.
}

void CalculatorActivity::drawCalculator(Canvas& c, Rect area) const {
  // Display: expression above, result big; inverted.
  const Rect display = area.takeTop(150);
  c.fill(display, Tone::Black);
  Rect inner = display.inset(BORDER + 12, 10);
  c.textIn(inner.takeTop(c.lineHeight(font::BODY) + 4), font::BODY, shownExpr_.c_str(), Tone::White, Align::Right);
  figure(c, inner, result_, Tone::White);
  area.takeTop(GAP + 4);

  // Backspace and % above the grid.
  const Rect top = area.takeTop(52);
  const int sw = (top.w - GAP) / 2;
  button(c, {top.x, top.y, sw, top.h}, "%", KEY, ButtonStyle::Quiet, K_PCT, font::TITLE);
  glyphButton(c, {top.x + sw + GAP, top.y, sw, top.h}, Glyph::Backspace, KEY, ButtonStyle::Quiet, K_BACK);
  area.takeTop(GAP);

  const int cols = 4;
  const int rows = KEY_COUNT / cols;
  const int w = (area.w - GAP * (cols - 1)) / cols;
  const int h = (area.h - GAP * (rows - 1)) / rows;
  for (int i = 0; i < KEY_COUNT; ++i) {
    const int8_t k = KEYS[i];
    const Rect r{area.x + (i % cols) * (w + GAP), area.y + (i / cols) * (h + GAP), w, h};
    const bool op = k == K_DIV || k == K_MUL || k == K_SUB || k == K_ADD;
    const ButtonStyle style = k == K_EQ            ? ButtonStyle::Primary
                              : op || k >= K_CLEAR ? ButtonStyle::Quiet
                                                   : ButtonStyle::Secondary;
    const char sep[2] = {decimalSeparator(), ' '};
    button(c, r, k == K_DOT ? sep : keyLabel(k), KEY, style, k, font::LARGE);
  }
}

// --- converter ------------------------------------------------------------------

void CalculatorActivity::loadUnits() {
  units_.clear();
  ratesNote_.clear();
  const auto add = [this](const UnitDef* defs, const size_t n) {
    for (size_t i = 0; i < n; ++i) units_.push_back({defs[i].name, defs[i].factor});
  };
  switch (category_) {
    case 0:
      add(LENGTH, sizeof(LENGTH) / sizeof(LENGTH[0]));
      break;
    case 1:
      add(WEIGHT, sizeof(WEIGHT) / sizeof(WEIGHT[0]));
      break;
    case 2:
      add(VOLUME, sizeof(VOLUME) / sizeof(VOLUME[0]));
      break;
    case TEMPERATURE:
      add(TEMPS, sizeof(TEMPS) / sizeof(TEMPS[0]));
      break;
    default: {
      JsonDocument doc;
      if (loadJson(sync::cachePath(AppId::Calculator, "cache-rates").c_str(), doc)) {
        units_.push_back({"EUR", 1});
        for (JsonPairConst p : doc["data"]["rates"].as<JsonObjectConst>()) {
          const double rate = p.value().as<double>();
          if (rate > 0) units_.push_back({p.key().c_str(), rate});
        }
        const char* date = doc["data"]["date"] | "";
        ratesNote_ = std::string("Rates of ") + date + ", updated at each sync";
      } else {
        ratesNote_ = "No rates yet: connect once to download them.";
      }
      break;
    }
  }
  const int n = static_cast<int>(units_.size());
  if (from_ >= n) from_ = 0;
  if (to_ >= n) to_ = n > 1 ? 1 : 0;
}

double CalculatorActivity::convert(const double value) const {
  if (units_.empty()) return 0;
  const Unit& a = units_[from_];
  const Unit& b = units_[to_];
  if (category_ == TEMPERATURE) {
    double celsius = value;
    if (a.name == "K") celsius = value - 273.15;
    if (a.name != "K" && a.name.back() == 'F') celsius = (value - 32) * 5 / 9;
    if (b.name == "K") return celsius + 273.15;
    if (b.name.back() == 'F') return celsius * 9 / 5 + 32;
    return celsius;
  }
  if (category_ == CURRENCY) return value / a.factor * b.factor;
  return value * a.factor / b.factor;
}

void CalculatorActivity::updateConvert() {
  std::string typed = keypad_.text().empty() ? "0" : keypad_.text();
  const double value = strtod(typed.c_str(), nullptr) * (negative_ ? -1 : 1);
  for (char& ch : typed) {
    if (ch == '.') ch = decimalSeparator();
  }
  valueText_ = (negative_ ? "\xE2\x88\x92" : "") + typed;
  if (units_.empty()) {
    resultText_ = "--";
  } else if (category_ == CURRENCY) {
    // Money: cents, grouped.
    const double v = convert(value);
    resultText_ = (v < 0 ? "â" : "") + formatDecimal(std::llround(std::fabs(v) * 100));
  } else {
    resultText_ = calc::formatNumber(convert(value));
  }
}

void CalculatorActivity::drawConverter(Canvas& c, Rect area) {
  // Categories as chips: five words do not fit a segmented control.
  int x = area.x;
  for (int i = 0; i < CATEGORY_COUNT; ++i) x += chip(c, x, area.y, CATEGORIES[i], i == category_, CATEGORY, i) + 6;
  area.takeTop(CHIP_H + GAP);

  const int rowH = 92;
  const int unitW = 120;
  // From: unit button and the typed value.
  Rect fromRow = area.takeTop(rowH);
  const Rect fromUnit = fromRow.takeLeft(unitW);
  fromRow.takeLeft(GAP);
  button(c, fromUnit, units_.empty() ? "-" : units_[from_].name.c_str(), PICK_FROM, ButtonStyle::Secondary, 0,
         font::TITLE);
  const Rect fromFace = block(c, fromRow, Tone::White);
  figure(c, fromFace.inset(12, 4), valueText_, Tone::Black);
  area.takeTop(6);

  // Swap in the middle.
  const Rect swapRow = area.takeTop(44);
  button(c, {swapRow.x, swapRow.y, unitW, 44}, "Swap", SWAP, ButtonStyle::Quiet, 0, font::SMALL);
  if (category_ == TEMPERATURE) {
    button(c, {swapRow.x + unitW + GAP, swapRow.y, 80, 44}, "\xC2\xB1", NEGATE, ButtonStyle::Quiet, 0, font::TITLE);
  }
  area.takeTop(6);

  // To: unit button and the result, inverted.
  Rect toRow = area.takeTop(rowH);
  const Rect toUnit = toRow.takeLeft(unitW);
  toRow.takeLeft(GAP);
  button(c, toUnit, units_.size() > 1 ? units_[to_].name.c_str() : "-", PICK_TO, ButtonStyle::Secondary, 0,
         font::TITLE);
  c.fill(toRow, Tone::Black);
  figure(c, toRow.inset(BORDER + 12, 4), resultText_, Tone::White);
  area.takeTop(GAP);

  if (!ratesNote_.empty()) {
    c.textIn(area.takeTop(c.lineHeight(font::SMALL) + 4), font::SMALL, ratesNote_.c_str(), Tone::Dark);
  }
  area.takeTop(GAP);
  keypad_.draw(c, area, VALUE_KEY);
}

void CalculatorActivity::drawPicker(Canvas& c) const {
  c.hit({0, 0, c.width(), c.height()}, CLOSE_PICKER);
  const int cols = 3;
  const int rows = (static_cast<int>(units_.size()) + cols - 1) / cols;
  const int cellH = 60;
  const int w = c.width() - 2 * GUTTER;
  const int h = std::min(c.height() - 160, rows * (cellH + GAP) + 2 * GUTTER + c.lineHeight(font::TITLE) + GAP);
  const Rect card{GUTTER, (c.height() - h) / 2, w, h};
  c.fill(card.offset(SHADOW * 2, SHADOW * 2), Tone::Black);
  c.fill(card, Tone::White);
  c.frame(card, BORDER, Tone::Black);
  Rect inner = card.inset(GUTTER);
  c.textIn(inner.takeTop(c.lineHeight(font::TITLE)), font::TITLE, picking_ == 1 ? "Convert from" : "Convert to",
           Tone::Black, Align::Left, true);
  inner.takeTop(GAP);
  const int cw = (inner.w - GAP * (cols - 1)) / cols;
  const int selected = picking_ == 1 ? from_ : to_;
  for (size_t i = 0; i < units_.size(); ++i) {
    const Rect r{inner.x + static_cast<int>(i % cols) * (cw + GAP),
                 inner.y + static_cast<int>(i / cols) * (cellH + GAP), cw, cellH};
    if (r.bottom() > inner.bottom()) break;
    button(c, r, units_[i].name.c_str(), PICKED,
           static_cast<int>(i) == selected ? ButtonStyle::Primary : ButtonStyle::Secondary, static_cast<int16_t>(i),
           font::BODY);
  }
}

// --- shared ---------------------------------------------------------------------

void CalculatorActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Calculator", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  area = area.inset(GUTTER, 0);
  area.takeBottom(GUTTER);
  segmented(c, area.takeTop(TAB_H), TABS, 2, tab_, TAB);
  area.takeTop(GAP);
  if (tab_ == 0) {
    drawCalculator(c, area);
  } else {
    drawConverter(c, area);
    if (picking_) drawPicker(c);
  }
}

void CalculatorActivity::onAction(const uint16_t action, const int16_t value) {
  if (picking_ && action != PICKED && action != CLOSE_PICKER) return;
  switch (action) {
    case BACK:
      onBack();
      return;
    case TAB:
      tab_ = value;
      picking_ = 0;
      return;
    case KEY:
      press(value);
      return;
    case CATEGORY:
      if (value >= 0 && value < CATEGORY_COUNT && value != category_) {
        category_ = value;
        from_ = DEFAULT_PAIR[value][0];
        to_ = DEFAULT_PAIR[value][1];
        negative_ = false;
        loadUnits();
        updateConvert();
      }
      return;
    case PICK_FROM:
    case PICK_TO:
      if (!units_.empty()) picking_ = action == PICK_FROM ? 1 : 2;
      return;
    case PICKED:
      if (value >= 0 && value < static_cast<int>(units_.size())) (picking_ == 1 ? from_ : to_) = value;
      picking_ = 0;
      updateConvert();
      refresh(true);
      return;
    case CLOSE_PICKER:
      picking_ = 0;
      refresh(true);
      return;
    case SWAP:
      std::swap(from_, to_);
      updateConvert();
      return;
    case NEGATE:
      negative_ = !negative_;
      updateConvert();
      return;
    case VALUE_KEY:
      if (keypad_.press(value) == Keypad::Event::Changed) updateConvert();
      return;
    default:
      return;
  }
}

bool CalculatorActivity::onUp() {
  if (tab_ == 0) return false;
  tab_ = 0;
  picking_ = 0;
  return true;
}

bool CalculatorActivity::onDown() {
  if (tab_ == 1) return false;
  tab_ = 1;
  return true;
}

void CalculatorActivity::onTick() {
  // Fresh rates arrived.
  const uint32_t gen = sync::generation();
  if (gen == seenGen_ || sync::busy()) return;
  seenGen_ = gen;
  if (category_ == CURRENCY) {
    loadUnits();
    updateConvert();
    refresh();
  }
}

}  // namespace fos::calc
