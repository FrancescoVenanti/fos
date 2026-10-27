#include "Keyboard.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "Components.h"

namespace fos::ui {

using namespace theme;

namespace {

// Control keys are single control bytes in the layout tables.
constexpr const char* K_SHIFT = "\x01";
constexpr const char* K_BACK = "\x02";
constexpr const char* K_ENTER = "\x03";
constexpr const char* K_PAGE = "\x04";
constexpr const char* K_SPACE = " ";

constexpr const char* QWERTY[3][10] = {{"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                                       {"a", "s", "d", "f", "g", "h", "j", "k", "l", nullptr},
                                       {"\x01", "z", "x", "c", "v", "b", "n", "m", "\x02", nullptr}};
constexpr const char* QWERTZ[3][10] = {{"q", "w", "e", "r", "t", "z", "u", "i", "o", "p"},
                                       {"a", "s", "d", "f", "g", "h", "j", "k", "l", nullptr},
                                       {"\x01", "y", "x", "c", "v", "b", "n", "m", "\x02", nullptr}};
constexpr const char* AZERTY[3][10] = {{"a", "z", "e", "r", "t", "y", "u", "i", "o", "p"},
                                       {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m"},
                                       {"\x01", "w", "x", "c", "v", "b", "n", "\x02", nullptr, nullptr}};
constexpr const char* SYMBOLS[3][10] = {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                                        {"-", "/", ":", ";", "(", ")", "\xE2\x82\xAC", "&", "@", "\""},
                                        {"#", "=", "+", "?", "!", "'", "*", "%", "\x02", nullptr}};
constexpr const char* ACCENTS[8] = {"\xC3\xA0", "\xC3\xA8", "\xC3\xA9", "\xC3\xAC",
                                    "\xC3\xB2", "\xC3\xB9", "\xC3\xA7", "'"};  // à è é ì ò ù ç '
constexpr const char* BOTTOM[5] = {"\x04", ",", " ", ".", "\x03"};

constexpr int KEY_H = 60;
constexpr int KEY_GAP = 5;

int weight(const char* key) {
  if (key == nullptr) return 0;
  if (strcmp(key, K_SPACE) == 0) return 9;
  if (strcmp(key, K_SHIFT) == 0 || strcmp(key, K_BACK) == 0 || strcmp(key, K_PAGE) == 0) return 3;
  if (strcmp(key, K_ENTER) == 0) return 4;
  return 2;
}

bool isControl(const char* key) { return key && key[0] > 0 && key[0] < 0x05 && key[1] == '\0'; }

// Upper-cases ASCII and the Latin-1 accented letters in the row.
std::string upper(const char* key) {
  std::string out(key);
  if (out.size() == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 32);
  if (out.size() == 2 && static_cast<uint8_t>(out[0]) == 0xC3 && static_cast<uint8_t>(out[1]) >= 0xA0 &&
      static_cast<uint8_t>(out[1]) <= 0xBE) {
    out[1] = static_cast<char>(static_cast<uint8_t>(out[1]) - 0x20);
  }
  return out;
}

void popUtf8(std::string& text) {
  if (text.empty()) return;
  size_t i = text.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i);
}

}  // namespace

Keyboard::Keyboard(std::string text, const size_t maxLength) : text_(std::move(text)), maxLength_(maxLength) {
  if (!text_.empty()) shift_ = Shift::Off;
}

bool Keyboard::hasAccentRow() const { return page_ == Page::Letters && settings().accentedLetters; }

int Keyboard::rowCount() const { return hasAccentRow() ? 5 : 4; }

int Keyboard::height() const { return rowCount() * (KEY_H + KEY_GAP); }

const char* const* Keyboard::row(int index, int& count) const {
  if (hasAccentRow()) {
    if (index == 0) {
      count = 8;
      return ACCENTS;
    }
    --index;
  }
  if (index == 3) {
    count = 5;
    return BOTTOM;
  }
  const char* const(*table)[10] = SYMBOLS;
  if (page_ == Page::Letters) {
    switch (settings().keyboardLayout) {
      case KeyboardLayout::Qwertz:
        table = QWERTZ;
        break;
      case KeyboardLayout::Azerty:
        table = AZERTY;
        break;
      case KeyboardLayout::Qwerty:
      default:
        table = QWERTY;
        break;
    }
  }
  count = 0;
  while (count < 10 && table[index][count]) ++count;
  return table[index];
}

void Keyboard::draw(Canvas& c, const Rect& r, const uint16_t action) const {
  const int rows = rowCount();
  for (int ri = 0; ri < rows; ++ri) {
    int count = 0;
    const char* const* keys = row(ri, count);
    int total = 0;
    for (int i = 0; i < count; ++i) total += weight(keys[i]);
    const int y = r.y + ri * (KEY_H + KEY_GAP);
    int x = r.x;
    int used = 0;
    for (int i = 0; i < count; ++i) {
      used += weight(keys[i]);
      const int x1 = r.x + (r.w + KEY_GAP) * used / total - KEY_GAP;
      const Rect key{x, y, x1 - x, KEY_H};
      x = x1 + KEY_GAP;
      const int16_t value = static_cast<int16_t>(ri * 16 + i);
      const char* k = keys[i];
      if (strcmp(k, K_SHIFT) == 0) {
        const bool on = shift_ != Shift::Off;
        const Rect face = block(c, key, on ? Tone::Black : Tone::Light, action, value);
        drawGlyph(c, face, Glyph::Up, on ? Tone::White : Tone::Black);
        if (shift_ == Shift::Locked) c.fill({face.x + face.w / 2 - 6, face.bottom() - 8, 12, 3}, Tone::White);
      } else if (strcmp(k, K_BACK) == 0) {
        drawGlyph(c, block(c, key, Tone::Light, action, value), Glyph::Backspace, Tone::Black);
      } else if (strcmp(k, K_ENTER) == 0) {
        const Rect face = block(c, key, Tone::Black, action, value);
        if (multiline_) {
          drawGlyph(c, face, Glyph::Back, Tone::White);  // a return arrow
        } else {
          c.textIn(face, font::SMALL, "OK", Tone::White, Align::Center, true);
        }
      } else if (strcmp(k, K_PAGE) == 0) {
        const Rect face = block(c, key, Tone::Light, action, value);
        c.textIn(face, font::SMALL, page_ == Page::Letters ? "123" : "ABC", Tone::Black, Align::Center, true);
      } else if (strcmp(k, K_SPACE) == 0) {
        const Rect face = block(c, key, Tone::White, action, value);
        c.textIn(face, font::SMALL, "space", Tone::Dark, Align::Center, true);
      } else {
        const Rect face = block(c, key, Tone::White, action, value);
        const std::string label = shift_ != Shift::Off && page_ == Page::Letters ? upper(k) : std::string(k);
        c.textIn(face, font::BODY, label.c_str(), Tone::Black, Align::Center, true);
      }
    }
  }
}

Keyboard::Event Keyboard::press(const int16_t value) {
  const int ri = value / 16;
  const int ci = value % 16;
  int count = 0;
  if (ri < 0 || ri >= rowCount()) return Event::None;
  const char* const* keys = row(ri, count);
  if (ci < 0 || ci >= count) return Event::None;
  const char* k = keys[ci];

  if (isControl(k)) {
    if (strcmp(k, K_SHIFT) == 0) {
      const unsigned long now = millis();
      if (shift_ == Shift::Once && now - lastShiftAt_ < 500) {
        shift_ = Shift::Locked;
      } else {
        shift_ = shift_ == Shift::Off ? Shift::Once : Shift::Off;
      }
      lastShiftAt_ = now;
      return Event::Changed;
    }
    if (strcmp(k, K_BACK) == 0) {
      popUtf8(text_);
      return Event::Changed;
    }
    if (strcmp(k, K_ENTER) == 0) {
      if (!multiline_) return Event::Enter;
      if (text_.size() >= maxLength_) return Event::None;
      text_ += '\n';
      return Event::Changed;
    }
    if (strcmp(k, K_PAGE) == 0) {
      page_ = page_ == Page::Letters ? Page::Symbols : Page::Letters;
      return Event::Changed;
    }
  }
  const std::string add = shift_ != Shift::Off && page_ == Page::Letters ? upper(k) : std::string(k);
  if (text_.size() + add.size() <= maxLength_) text_ += add;
  if (shift_ == Shift::Once) shift_ = Shift::Off;
  return Event::Changed;
}

// --- Keypad ---------------------------------------------------------------

char decimalSeparator() { return settings().decimalComma ? ',' : '.'; }

Keypad::Keypad(std::string text, const int maxDecimals, const size_t maxLength)
    : text_(std::move(text)), maxDecimals_(maxDecimals), maxLength_(maxLength) {}

void Keypad::draw(Canvas& c, const Rect& r, const uint16_t action) const {
  static constexpr const char* LABELS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", ""};
  const int w = (r.w - 2 * GAP) / 3;
  const int h = (r.h - 3 * GAP) / 4;
  for (int i = 0; i < 12; ++i) {
    const Rect key{r.x + (i % 3) * (w + GAP), r.y + (i / 3) * (h + GAP), w, h};
    if (i == 9) {
      if (maxDecimals_ == 0) continue;
      const Rect face = block(c, key, Tone::Light, action, static_cast<int16_t>(i));
      const char sep[2] = {decimalSeparator(), '\0'};
      c.textIn(face, font::LARGE, sep, Tone::Black, Align::Center, true);
    } else if (i == 11) {
      drawGlyph(c, block(c, key, Tone::Light, action, static_cast<int16_t>(i)), Glyph::Backspace, Tone::Black);
    } else {
      const Rect face = block(c, key, Tone::White, action, static_cast<int16_t>(i));
      c.textIn(face, font::LARGE, LABELS[i], Tone::Black, Align::Center, true);
    }
  }
}

Keypad::Event Keypad::press(const int16_t value) {
  if (value == 11) {
    if (!text_.empty()) text_.pop_back();
    return Event::Changed;
  }
  const size_t dot = text_.find('.');
  if (value == 9) {
    if (maxDecimals_ == 0 || dot != std::string::npos) return Event::None;
    text_ += text_.empty() ? "0." : ".";
    return Event::Changed;
  }
  if (value < 0 || value > 10) return Event::None;
  const char digit = value == 10 ? '0' : static_cast<char>('1' + value);
  if (text_.size() >= maxLength_) return Event::None;
  if (dot != std::string::npos && static_cast<int>(text_.size() - dot - 1) >= maxDecimals_) return Event::None;
  if (text_ == "0") text_.clear();
  text_ += digit;
  return Event::Changed;
}

long long Keypad::scaled(const int decimals) const {
  long long whole = 0;
  long long frac = 0;
  int fracDigits = 0;
  bool afterDot = false;
  for (const char ch : text_) {
    if (ch == '.') {
      afterDot = true;
    } else if (ch >= '0' && ch <= '9') {
      if (!afterDot) {
        whole = whole * 10 + (ch - '0');
      } else if (fracDigits < decimals) {
        frac = frac * 10 + (ch - '0');
        ++fracDigits;
      }
    }
  }
  for (; fracDigits < decimals; ++fracDigits) frac *= 10;
  long long scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  return whole * scale + frac;
}

std::string formatDecimal(const long long scaled, const int decimals, const bool thousands) {
  long long scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  const bool negative = scaled < 0;
  const unsigned long long v = negative ? static_cast<unsigned long long>(-scaled) : scaled;
  const unsigned long long whole = v / scale;
  const unsigned long long frac = v % scale;

  char digits[24];
  snprintf(digits, sizeof(digits), "%llu", whole);
  std::string out = negative ? "-" : "";
  const size_t len = strlen(digits);
  const char group = settings().decimalComma ? '.' : ',';
  for (size_t i = 0; i < len; ++i) {
    if (thousands && i > 0 && (len - i) % 3 == 0) out += group;
    out += digits[i];
  }
  if (decimals > 0) {
    char fracText[12];
    snprintf(fracText, sizeof(fracText), "%0*llu", decimals, frac);
    out += decimalSeparator();
    out += fracText;
  }
  return out;
}

}  // namespace fos::ui
