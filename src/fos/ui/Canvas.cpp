#include "Canvas.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>
#include <vector>

namespace fos::ui {

Rect Rect::takeTop(const int height) {
  const Rect top{x, y, w, height};
  y += height;
  h -= height;
  return top;
}

Rect Rect::takeBottom(const int height) {
  h -= height;
  return {x, y + h, w, height};
}

Rect Rect::takeLeft(const int width) {
  const Rect left{x, y, width, h};
  x += width;
  w -= width;
  return left;
}

Rect Rect::takeRight(const int width) {
  w -= width;
  return {x + w, y, width, h};
}

void HitList::add(const Rect& rect, const uint16_t action, const int16_t value) {
  if (count_ >= CAPACITY || rect.w <= 0 || rect.h <= 0) return;
  hits_[count_++] = {rect, action, value};
}

const Hit* HitList::find(const int x, const int y) const {
  for (int i = count_ - 1; i >= 0; --i) {
    if (hits_[i].rect.contains(x, y)) return &hits_[i];
  }
  return nullptr;
}

Canvas::Canvas(const GfxRenderer& renderer, const Pass pass, const bool inverted, HitList* hits,
               const uint16_t pressedAction, const int16_t pressedValue)
    : renderer_(renderer),
      pass_(pass),
      inverted_(inverted),
      hits_(hits),
      pressedAction_(pressedAction),
      pressedValue_(pressedValue) {}

int Canvas::width() const { return renderer_.getScreenWidth(); }

int Canvas::height() const { return renderer_.getScreenHeight(); }

Tone Canvas::resolve(const Tone tone) {
  const Tone t = inverted_ ? static_cast<Tone>(3 - static_cast<int>(tone)) : tone;
  if (t == Tone::Light || t == Tone::Dark) usedGray_ = true;
  return t;
}

bool Canvas::ink(const Tone tone) {
  const Tone t = resolve(tone);
  switch (pass_) {
    case Pass::Bw:
      // The B/W base shows dark gray as black and light gray as white.
      return t == Tone::Dark || t == Tone::Black;
    case Pass::GrayLsb:
      // LSB plane bit is set for dark gray and white.
      return !(t == Tone::Dark || t == Tone::White);
    case Pass::GrayMsb:
      // MSB plane bit is set for light gray and white.
      return !(t == Tone::Light || t == Tone::White);
  }
  return true;
}

void Canvas::fill(const Rect& rect, const Tone tone) {
  if (rect.w <= 0 || rect.h <= 0) return;
  renderer_.fillRect(rect.x, rect.y, rect.w, rect.h, ink(tone));
}

void Canvas::frame(const Rect& rect, const int thickness, const Tone tone) {
  if (rect.w <= 0 || rect.h <= 0 || thickness <= 0) return;
  const bool state = ink(tone);
  const int t = std::min({thickness, rect.w / 2 + 1, rect.h / 2 + 1});
  renderer_.fillRect(rect.x, rect.y, rect.w, t, state);
  renderer_.fillRect(rect.x, rect.bottom() - t, rect.w, t, state);
  renderer_.fillRect(rect.x, rect.y + t, t, rect.h - 2 * t, state);
  renderer_.fillRect(rect.right() - t, rect.y + t, t, rect.h - 2 * t, state);
}

void Canvas::line(const int x1, const int y1, const int x2, const int y2, const int thickness, const Tone tone) {
  renderer_.drawLine(x1, y1, x2, y2, thickness, ink(tone));
}

void Canvas::pixel(const int x, const int y, const Tone tone) { renderer_.drawPixel(x, y, ink(tone)); }

void Canvas::bits(const uint8_t* data, const int x, const int y, const int w, const int h, const Tone tone,
                  const int scale) {
  if (!data || w <= 0 || h <= 0) return;
  const bool state = ink(tone);
  const int stride = (w + 7) / 8;
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      if ((data[row * stride + col / 8] & (0x80 >> (col % 8))) == 0) continue;
      if (scale == 1) {
        renderer_.drawPixel(x + col, y + row, state);
      } else {
        renderer_.fillRect(x + col * scale, y + row * scale, scale, scale, state);
      }
    }
  }
}

void Canvas::triangle(const int x1, const int y1, const int x2, const int y2, const int x3, const int y3,
                      const Tone tone) {
  const int xs[3] = {x1, x2, x3};
  const int ys[3] = {y1, y2, y3};
  renderer_.fillPolygon(xs, ys, 3, ink(tone));
}

void Canvas::text(const int fontId, const int x, const int y, const char* text, const Tone tone, const bool bold) {
  if (!text || !*text) return;
  renderer_.drawText(fontId, x, y, text, ink(tone), style(bold));
}

int Canvas::scaledWidth(const int fontId, const char* text, const int scale256) const {
  int width = 0;
  for (const char* p = text; p && *p; ++p) {
    int32_t advance = 0;
    int top = 0;
    if (renderer_.getCodepointMetrics(fontId, static_cast<uint8_t>(*p), EpdFontFamily::REGULAR, advance, top)) {
      width += static_cast<int>(advance * scale256 / 256 / 16);
    }
  }
  return width;
}

int Canvas::scaledText(const int fontId, const int x, const int baseline, const char* text, const int scale256) {
  const bool black = ink(Tone::Black);
  int cx = x;
  for (const char* p = text; p && *p; ++p) {
    cx += renderer_.drawScaledCodepoint(fontId, static_cast<uint8_t>(*p), EpdFontFamily::REGULAR, cx, baseline,
                                        scale256, black);
  }
  return cx - x;
}

void Canvas::textIn(const Rect& rect, const int fontId, const char* text, const Tone tone, const Align align,
                    const bool bold) {
  if (!text || !*text || rect.w <= 0) return;
  std::string fitted;
  const char* shown = text;
  int width = textWidth(fontId, text, bold);
  if (width > rect.w) {
    fitted = renderer_.truncatedText(fontId, text, rect.w, style(bold));
    shown = fitted.c_str();
    width = textWidth(fontId, shown, bold);
  }
  int x = rect.x;
  if (align == Align::Center) x = rect.x + (rect.w - width) / 2;
  if (align == Align::Right) x = rect.right() - width;
  const int y = rect.y + (rect.h - lineHeight(fontId)) / 2;
  this->text(fontId, x, y, shown, tone, bold);
}

int Canvas::textWrapped(const Rect& rect, const int fontId, const char* text, const Tone tone, const int maxLines,
                        const bool bold) {
  if (!text || !*text) return 0;
  const std::vector<std::string> lines = renderer_.wrappedText(fontId, text, rect.w, maxLines, style(bold));
  const int lh = lineHeight(fontId);
  int y = rect.y;
  for (const auto& line : lines) {
    this->text(fontId, rect.x, y, line.c_str(), tone, bold);
    y += lh;
  }
  return static_cast<int>(lines.size()) * lh;
}

int Canvas::textWidth(const int fontId, const char* text, const bool bold) const {
  if (!text || !*text) return 0;
  return renderer_.getTextWidth(fontId, text, style(bold));
}

int Canvas::lineHeight(const int fontId) const { return renderer_.getLineHeight(fontId); }

int Canvas::ascender(const int fontId) const { return renderer_.getFontAscenderSize(fontId); }

void Canvas::hit(const Rect& rect, const uint16_t action, const int16_t value) {
  if (hits_ && action != 0) hits_->add(rect, action, value);
}

}  // namespace fos::ui
