#include "Branding.h"

#include <GfxRenderer.h>

#include "Canvas.h"
#include "Components.h"
#include "fos/core/Version.h"

namespace fos::ui {

using namespace theme;

namespace {

constexpr const char* WORDMARK = "FOs";
constexpr int WORDMARK_SCALE = 512;  // 2x the 26 pt display font

int wordmarkWidth(const GfxRenderer& r) {
  int width = 0;
  for (const char* p = WORDMARK; *p; ++p) {
    int32_t advance = 0;
    int top = 0;
    if (r.getCodepointMetrics(font::LARGE, static_cast<uint8_t>(*p), EpdFontFamily::REGULAR, advance, top)) {
      width += static_cast<int>(advance * WORDMARK_SCALE / 256 / 16);
    }
  }
  return width;
}

// The wordmark card centered at (cx, cy); returns its bottom edge.
int wordmarkCard(const GfxRenderer& r, Canvas& c, const int cx, const int cy) {
  const int w = 300;
  const int h = 190;
  const Rect card{cx - w / 2, cy - h / 2, w, h};
  const Rect face = block(c, card, Tone::White);
  const int ascent = r.getFontAscenderSize(font::LARGE) * WORDMARK_SCALE / 256;
  int x = face.x + (face.w - wordmarkWidth(r)) / 2;
  const int baseline = face.y + (face.h + ascent) / 2 - 8;
  for (const char* p = WORDMARK; *p; ++p) {
    x += r.drawScaledCodepoint(font::LARGE, static_cast<uint8_t>(*p), EpdFontFamily::REGULAR, x, baseline,
                               WORDMARK_SCALE);
  }
  // Heavy underline bar, the brutalist accent.
  c.fill({face.x + 40, face.bottom() - 34, face.w - 80, 8}, Tone::Black);
  return card.bottom();
}

}  // namespace

void drawBootScreen(const GfxRenderer& renderer, const char* status) {
  renderer.clearScreen();
  Canvas c(renderer, Pass::Bw, false, nullptr, 0, 0);
  const int bottom = wordmarkCard(renderer, c, c.width() / 2, c.height() / 2 - 40);
  c.textIn({0, bottom + 24, c.width(), 40}, font::BODY, status, Tone::Black, Align::Center, true);
  c.textIn({0, c.height() - 60, c.width(), 40}, font::SMALL, FOS_VERSION_STRING, Tone::Black, Align::Center);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void drawSleepScreen(const GfxRenderer& renderer) {
  renderer.clearScreen();
  Canvas c(renderer, Pass::Bw, false, nullptr, 0, 0);
  const int bottom = wordmarkCard(renderer, c, c.width() / 2, c.height() / 2 - 40);
  c.textIn({0, bottom + 24, c.width(), 40}, font::BODY, "Sleeping. Press OK to wake.", Tone::Black, Align::Center,
           true);
}

}  // namespace fos::ui
