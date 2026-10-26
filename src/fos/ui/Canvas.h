#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

class GfxRenderer;

namespace fos::ui {

// The panel's four gray levels. Ordered from lightest to darkest.
enum class Tone : uint8_t { White = 0, Light = 1, Dark = 2, Black = 3 };

// Which framebuffer a draw call is producing. A frame with grays is drawn once
// per pass: the B/W base, then the two absolute gray planes.
enum class Pass : uint8_t { Bw, GrayLsb, GrayMsb };

enum class Align : uint8_t { Left, Center, Right };

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
  Rect inset(int dx, int dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
  Rect offset(int dx, int dy) const { return {x + dx, y + dy, w, h}; }
  // Splits off the top/left part; the remainder stays in *this.
  Rect takeTop(int height);
  Rect takeBottom(int height);
  Rect takeLeft(int width);
  Rect takeRight(int width);
};

// Tappable areas registered while a screen draws, in draw order. Later
// entries are on top, so lookups search from the end.
struct Hit {
  Rect rect;
  uint16_t action = 0;
  int16_t value = 0;
};

class HitList {
 public:
  static constexpr int CAPACITY = 192;

  void clear() { count_ = 0; }
  void add(const Rect& rect, uint16_t action, int16_t value);
  const Hit* find(int x, int y) const;
  int size() const { return count_; }

 private:
  Hit hits_[CAPACITY];
  int count_ = 0;
};

// Draws one pass of a frame. Every primitive takes a Tone and writes the bit
// that tone needs in the current pass (absolute 4-tone encoding: black (0,0),
// dark (1,0), light (0,1), white (1,1) for (LSB, MSB)), so fills are exact
// grays without dithering. Text is drawn as solid ink in every pass, so it
// stays crisp on any background.
class Canvas {
 public:
  Canvas(const GfxRenderer& renderer, Pass pass, bool inverted, HitList* hits, uint16_t pressedAction,
         int16_t pressedValue);

  int width() const;
  int height() const;
  Pass pass() const { return pass_; }
  const GfxRenderer& renderer() const { return renderer_; }

  void fill(const Rect& rect, Tone tone);
  void frame(const Rect& rect, int thickness, Tone tone);
  void line(int x1, int y1, int x2, int y2, int thickness, Tone tone);
  void pixel(int x, int y, Tone tone);
  // 1 bit per pixel, rows MSB first and byte aligned; set bits are drawn.
  void bits(const uint8_t* data, int x, int y, int w, int h, Tone tone, int scale = 1);
  void triangle(int x1, int y1, int x2, int y2, int x3, int y3, Tone tone);

  // y is the top of the line box.
  void text(int fontId, int x, int y, const char* text, Tone tone, bool bold = false);
  // Single line inside rect: vertically centered, ellipsized to fit.
  void textIn(const Rect& rect, int fontId, const char* text, Tone tone, Align align = Align::Left, bool bold = false);
  // Word-wrapped text; returns the height used.
  int textWrapped(const Rect& rect, int fontId, const char* text, Tone tone, int maxLines, bool bold = false);
  int textWidth(int fontId, const char* text, bool bold = false) const;
  // ASCII text scaled by scale256/256 (big clock digits), solid black in every
  // pass; y is the baseline. Returns the advance.
  int scaledText(int fontId, int x, int baseline, const char* text, int scale256);
  int scaledWidth(int fontId, const char* text, int scale256) const;
  int lineHeight(int fontId) const;
  int ascender(int fontId) const;

  void hit(const Rect& rect, uint16_t action, int16_t value = 0);
  bool pressed(uint16_t action, int16_t value = 0) const {
    return action != 0 && action == pressedAction_ && value == pressedValue_;
  }
  bool usedGray() const { return usedGray_; }

 private:
  Tone resolve(Tone tone);
  // Renderer "black" state for a tone in this pass.
  bool ink(Tone tone);
  EpdFontFamily::Style style(bool bold) const { return bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR; }

  const GfxRenderer& renderer_;
  Pass pass_;
  bool inverted_;
  HitList* hits_;
  uint16_t pressedAction_;
  int16_t pressedValue_;
  bool usedGray_ = false;
};

}  // namespace fos::ui
