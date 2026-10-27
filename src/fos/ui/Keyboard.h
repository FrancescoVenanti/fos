#pragma once

#include <string>

#include "Canvas.h"
#include "fos/settings/FosSettings.h"

namespace fos::ui {

// Compact on-screen keyboard. Layout (QWERTY/QWERTZ/AZERTY) and the accented
// letters row follow Settings > Buttons and keyboard.
class Keyboard {
 public:
  enum class Event : uint8_t { None, Changed, Enter };

  explicit Keyboard(std::string text = {}, size_t maxLength = 120);

  // Height the keyboard needs at full screen width.
  int height() const;
  // Draws the keys inside r and registers them under `action`.
  void draw(Canvas& c, const Rect& r, uint16_t action) const;
  // Handles a tapped key (the hit value registered by draw()).
  Event press(int16_t value);

  const std::string& text() const { return text_; }
  void setText(std::string text) { text_ = std::move(text); }
  // Multi-line: the Enter key types a new line instead of finishing.
  void setMultiline(bool on) { multiline_ = on; }

 private:
  enum class Page : uint8_t { Letters, Symbols };
  enum class Shift : uint8_t { Off, Once, Locked };

  int rowCount() const;
  // Keys of a row on the current page; special keys are single control bytes.
  const char* const* row(int index, int& count) const;
  bool hasAccentRow() const;

  std::string text_;
  size_t maxLength_;
  Page page_ = Page::Letters;
  Shift shift_ = Shift::Once;
  unsigned long lastShiftAt_ = 0;
  bool multiline_ = false;
};

// 3x4 numeric keypad: 1-9, decimal separator, 0, backspace. The separator is
// a comma when Settings > Region uses the decimal comma.
class Keypad {
 public:
  enum class Event : uint8_t { None, Changed };

  explicit Keypad(std::string text = {}, int maxDecimals = 2, size_t maxLength = 10);

  void draw(Canvas& c, const Rect& r, uint16_t action) const;
  Event press(int16_t value);

  const std::string& text() const { return text_; }  // always uses '.' internally
  void setText(std::string text) { text_ = std::move(text); }
  // Value in hundredths (cents) for 2 decimals; text "12.5" -> 1250.
  long long scaled(int decimals = 2) const;

 private:
  std::string text_;
  int maxDecimals_;
  size_t maxLength_;
};

// Formats a number with the user's decimal separator: 1234.5 -> "1234,50".
std::string formatDecimal(long long scaled, int decimals = 2, bool thousands = true);
char decimalSeparator();

}  // namespace fos::ui
