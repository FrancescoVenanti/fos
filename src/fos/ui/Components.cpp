#include "Components.h"

#include <algorithm>
#include <cstdio>

#include "fos/settings/FosSettings.h"

namespace fos::ui {

using namespace theme;

namespace {

int titleFont() {
  switch (settings().fontSize) {
    case FontSize::Small:
      return font::SMALL;
    case FontSize::Large:
      return font::TITLE;
    case FontSize::Medium:
    default:
      return font::BODY;
  }
}

void battery(Canvas& c, const int x, const int y, const int percent) {
  // 30x16 body with a 3x6 nub; fill proportional to charge.
  const Rect body{x, y, 30, 16};
  c.frame(body, 2, Tone::Black);
  c.fill({body.right(), y + 5, 3, 6}, Tone::Black);
  if (percent >= 0) {
    const int inner = 30 - 6;
    const int w = std::max(0, std::min(inner, inner * percent / 100));
    c.fill({x + 3, y + 3, w, 10}, Tone::Black);
  }
}

void wifiBars(Canvas& c, const int x, const int y, const bool on) {
  // Three rising bars; hollow and struck through when offline.
  for (int i = 0; i < 3; ++i) {
    const int h = 6 + i * 5;
    const Rect bar{x + i * 7, y + 16 - h, 5, h};
    if (on) {
      c.fill(bar, Tone::Black);
    } else {
      c.frame(bar, 1, Tone::Black);
    }
  }
  if (!on) c.line(x - 2, y + 17, x + 21, y - 1, 2, Tone::Black);
}

}  // namespace

void statusBar(Canvas& c, const StatusInfo& status) {
  const Rect r{0, 0, c.width(), STATUS_BAR_H};
  c.fill(r, Tone::White);
  rule(c, 0, STATUS_BAR_H - BORDER, c.width());
  const Rect content{GUTTER, 0, c.width() - 2 * GUTTER, STATUS_BAR_H - BORDER};
  c.textIn(content, font::SMALL, status.time, Tone::Black, Align::Left, true);

  int x = content.right() - 33;
  const int iconY = (STATUS_BAR_H - BORDER - 16) / 2;
  battery(c, x, iconY, status.battery);
  if (status.battery >= 0) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", status.battery);
    const int w = c.textWidth(font::SMALL, pct, true);
    x -= w + 8;
    c.textIn({x, 0, w, STATUS_BAR_H - BORDER}, font::SMALL, pct, Tone::Black, Align::Left, true);
  }
  x -= 34;
  wifiBars(c, x, iconY, status.wifi);

  const char* sync = nullptr;
  char buf[16];
  if (status.pending > 0) {
    snprintf(buf, sizeof(buf), "%d TO SYNC", status.pending);
    sync = buf;
  } else if (status.sync == SyncState::Syncing) {
    sync = "SYNCING";
  } else if (status.sync == SyncState::Error) {
    sync = "SYNC ERROR";
  }
  if (sync) {
    const int w = c.textWidth(font::SMALL, sync, true) + 12;
    x -= w + 10;
    const Rect tag{x, iconY - 3, w, 22};
    c.fill(tag, Tone::Black);
    c.textIn(tag, font::SMALL, sync, Tone::White, Align::Center, true);
  }
}

void header(Canvas& c, const Rect& r, const char* title, const uint16_t backAction, const char* rightLabel,
            const uint16_t rightAction, const Glyph rightGlyph) {
  Rect row = r.inset(GUTTER, 0);
  const int size = r.h - 16;
  if (backAction) {
    glyphButton(c, {row.x, r.y + 8, size, size}, Glyph::Back, backAction);
    row.takeLeft(size + GAP);
  }
  if (rightAction) {
    if (rightLabel) {
      const int w = std::max(size, c.textWidth(font::BODY, rightLabel, true) + 32);
      button(c, {row.right() - w, r.y + 8, w, size}, rightLabel, rightAction);
      row.takeRight(w + GAP);
    } else {
      glyphButton(c, {row.right() - size, r.y + 8, size, size}, rightGlyph, rightAction);
      row.takeRight(size + GAP);
    }
  }
  c.textIn(row, font::TITLE, title, Tone::Black, Align::Left, true);
}

Rect block(Canvas& c, const Rect& r, const Tone face, const uint16_t action, const int16_t value, const bool shadow) {
  const bool down = shadow && c.pressed(action, value);
  Rect f = shadow ? Rect{r.x, r.y, r.w - SHADOW, r.h - SHADOW} : r;
  if (down) {
    f = f.offset(SHADOW, SHADOW);
  } else if (shadow) {
    c.fill(f.offset(SHADOW, SHADOW), Tone::Black);
    // A black face would merge with its shadow; a white gap keeps the lift visible.
    if (face == Tone::Black) c.frame(f.inset(-BORDER), BORDER, Tone::White);
  }
  c.fill(f, face);
  c.frame(f, BORDER, Tone::Black);
  c.hit(r, action, value);
  return f.inset(BORDER);
}

void button(Canvas& c, const Rect& r, const char* label, const uint16_t action, const ButtonStyle style,
            const int16_t value, const int fontId) {
  const Tone face = style == ButtonStyle::Primary ? Tone::Black
                    : style == ButtonStyle::Quiet ? Tone::Light
                                                  : Tone::White;
  const Rect inner = block(c, r, face, action, value, style != ButtonStyle::Quiet);
  c.textIn(inner.inset(8, 0), fontId, label, style == ButtonStyle::Primary ? Tone::White : Tone::Black, Align::Center,
           true);
}

void glyphButton(Canvas& c, const Rect& r, const Glyph glyph, const uint16_t action, const ButtonStyle style,
                 const int16_t value) {
  const Tone face = style == ButtonStyle::Primary ? Tone::Black
                    : style == ButtonStyle::Quiet ? Tone::Light
                                                  : Tone::White;
  const Rect inner = block(c, r, face, action, value, style != ButtonStyle::Quiet);
  drawGlyph(c, inner, glyph, style == ButtonStyle::Primary ? Tone::White : Tone::Black);
}

void drawGlyph(Canvas& c, const Rect& r, const Glyph glyph, const Tone tone) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int s = std::max(6, std::min(r.w, r.h) * 3 / 10);  // half-size of the glyph
  const int t = std::max(3, s / 3);                        // stroke
  switch (glyph) {
    case Glyph::Back:
      c.triangle(cx - s, cy, cx, cy - s, cx, cy + s, tone);
      c.fill({cx, cy - t / 2, s, t}, tone);
      break;
    case Glyph::Forward:
      c.triangle(cx + s, cy, cx, cy - s, cx, cy + s, tone);
      c.fill({cx - s, cy - t / 2, s, t}, tone);
      break;
    case Glyph::Up:
      c.triangle(cx, cy - s, cx - s, cy + s / 2, cx + s, cy + s / 2, tone);
      break;
    case Glyph::Down:
      c.triangle(cx, cy + s, cx - s, cy - s / 2, cx + s, cy - s / 2, tone);
      break;
    case Glyph::Plus:
      c.fill({cx - s, cy - t / 2, 2 * s, t}, tone);
      c.fill({cx - t / 2, cy - s, t, 2 * s}, tone);
      break;
    case Glyph::Minus:
      c.fill({cx - s, cy - t / 2, 2 * s, t}, tone);
      break;
    case Glyph::Check:
      c.line(cx - s, cy, cx - s / 3, cy + s * 2 / 3, t, tone);
      c.line(cx - s / 3, cy + s * 2 / 3, cx + s, cy - s * 2 / 3, t, tone);
      break;
    case Glyph::Close:
      c.line(cx - s, cy - s, cx + s, cy + s, t, tone);
      c.line(cx - s, cy + s, cx + s, cy - s, t, tone);
      break;
    case Glyph::Menu:
      for (int i = -1; i <= 1; ++i) c.fill({cx - s, cy + i * s * 2 / 3 - t / 2, 2 * s, t}, tone);
      break;
    case Glyph::Play:
      c.triangle(cx - s * 2 / 3, cy - s, cx - s * 2 / 3, cy + s, cx + s, cy, tone);
      break;
    case Glyph::Pause:
      c.fill({cx - s * 2 / 3, cy - s, s / 2 + 1, 2 * s}, tone);
      c.fill({cx + s / 6, cy - s, s / 2 + 1, 2 * s}, tone);
      break;
    case Glyph::Backspace:
      c.triangle(cx - s, cy, cx - s / 3, cy - s * 2 / 3, cx - s / 3, cy + s * 2 / 3, tone);
      c.fill({cx - s / 3, cy - s * 2 / 3, s * 4 / 3, s * 4 / 3}, tone);
      break;
    case Glyph::None:
      break;
  }
}

void listRow(Canvas& c, const Rect& r, const char* title, const char* detail, const char* trailing,
             const uint16_t action, const int16_t value, const bool chevron) {
  const bool down = c.pressed(action, value);
  c.fill(r, down ? Tone::Black : Tone::White);
  const Tone ink = down ? Tone::White : Tone::Black;
  rule(c, r.x, r.bottom() - BORDER, r.w, BORDER, Tone::Black);
  c.hit(r, action, value);

  Rect content{r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER};
  if (chevron) {
    drawGlyph(c, content.takeRight(24), Glyph::Forward, ink);
    content.takeRight(8);
  }
  if (trailing && *trailing) {
    const int w = std::min(content.w / 2, c.textWidth(font::BODY, trailing, true) + 4);
    c.textIn(content.takeRight(w), font::BODY, trailing, ink, Align::Right, true);
    content.takeRight(12);
  }
  if (detail && *detail) {
    const int th = c.lineHeight(titleFont());
    const int dh = c.lineHeight(font::SMALL);
    const int top = content.y + (content.h - th - dh) / 2;
    c.textIn({content.x, top, content.w, th}, titleFont(), title, ink, Align::Left, true);
    c.textIn({content.x, top + th, content.w, dh}, font::SMALL, detail, down ? Tone::White : Tone::Dark);
  } else {
    c.textIn(content, titleFont(), title, ink, Align::Left, true);
  }
}

void sectionLabel(Canvas& c, const Rect& r, const char* text) {
  c.textIn(r.inset(GUTTER, 0), font::SMALL, text, Tone::Dark, Align::Left, true);
}

void toggle(Canvas& c, const Rect& track, const bool on) {
  // Square-cornered track; the knob is a white square that sits right when on.
  c.fill(track, on ? Tone::Black : Tone::White);
  c.frame(track, BORDER, Tone::Black);
  const int k = track.h - 2 * BORDER - 4;
  const int kx = on ? track.right() - BORDER - 2 - k : track.x + BORDER + 2;
  const Rect knob{kx, track.y + BORDER + 2, k, k};
  c.fill(knob, on ? Tone::White : Tone::Black);
}

void toggleRow(Canvas& c, const Rect& r, const char* label, const char* detail, const bool on, const uint16_t action,
               const int16_t value) {
  c.fill(r, Tone::White);
  rule(c, r.x, r.bottom() - BORDER, r.w);
  c.hit(r, action, value);
  Rect content{r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER};
  const Rect track = content.takeRight(68);
  toggle(c, {track.x, track.y + (track.h - 36) / 2, 68, 36}, on);
  content.takeRight(GAP);
  if (detail && *detail) {
    const int th = c.lineHeight(titleFont());
    const int dh = c.lineHeight(font::SMALL);
    const int top = content.y + (content.h - th - dh) / 2;
    c.textIn({content.x, top, content.w, th}, titleFont(), label, Tone::Black, Align::Left, true);
    c.textIn({content.x, top + th, content.w, dh}, font::SMALL, detail, Tone::Dark);
  } else {
    c.textIn(content, titleFont(), label, Tone::Black, Align::Left, true);
  }
}

void segmented(Canvas& c, const Rect& r, const char* const* labels, const int count, const int selected,
               const uint16_t action) {
  if (count <= 0) return;
  const Rect face = block(c, r, Tone::White, 0, 0, true).inset(-BORDER);
  const int inner = face.w - 2 * BORDER;
  for (int i = 0; i < count; ++i) {
    const int x0 = face.x + BORDER + inner * i / count;
    const int x1 = face.x + BORDER + inner * (i + 1) / count;
    const Rect seg{x0, face.y + BORDER, x1 - x0, face.h - 2 * BORDER};
    const bool on = i == selected;
    if (on) c.fill(seg, Tone::Black);
    if (i > 0) c.fill({x0 - 1, seg.y, BORDER, seg.h}, Tone::Black);
    c.textIn(seg.inset(4, 0), font::BODY, labels[i], on ? Tone::White : Tone::Black, Align::Center, true);
    c.hit(seg, action, static_cast<int16_t>(i));
  }
}

int chipWidth(Canvas& c, const char* label) { return c.textWidth(font::SMALL, label, true) + 28; }

int chip(Canvas& c, const int x, const int y, const char* label, const bool selected, const uint16_t action,
         const int16_t value) {
  const int w = chipWidth(c, label);
  const Rect r{x, y, w, CHIP_H};
  const bool down = c.pressed(action, value);
  const bool dark = selected || down;
  c.fill(r, dark ? Tone::Black : Tone::Light);
  c.frame(r, BORDER, Tone::Black);
  c.textIn(r, font::SMALL, label, dark ? Tone::White : Tone::Black, Align::Center, true);
  c.hit(r, action, value);
  return w;
}

void progressBar(Canvas& c, const Rect& r, const int value, const int max, const Tone fill) {
  c.fill(r, Tone::White);
  c.frame(r, BORDER, Tone::Black);
  if (max <= 0) return;
  const Rect inner = r.inset(BORDER);
  const int clamped = std::max(0, std::min(value, max));
  c.fill({inner.x, inner.y, inner.w * clamped / max, inner.h}, fill);
}

void rule(Canvas& c, const int x, const int y, const int w, const int thickness, const Tone tone) {
  c.fill({x, y, w, thickness}, tone);
}

void pager(Canvas& c, const Rect& r, const int page, const int pages, const uint16_t prevAction,
           const uint16_t nextAction) {
  const int size = r.h;
  if (page > 0) glyphButton(c, {r.x, r.y, size, size}, Glyph::Back, prevAction);
  if (page < pages - 1) glyphButton(c, {r.right() - size, r.y, size, size}, Glyph::Forward, nextAction);
  char label[16];
  snprintf(label, sizeof(label), "%d / %d", page + 1, pages);
  c.textIn({r.x + size, r.y, r.w - 2 * size, size - SHADOW}, font::BODY, label, Tone::Black, Align::Center, true);
}

void emptyState(Canvas& c, const Rect& r, const char* title, const char* text) {
  const int th = c.lineHeight(font::TITLE);
  const int y = r.y + r.h / 2 - th;
  c.textIn({r.x + GUTTER, y, r.w - 2 * GUTTER, th}, font::TITLE, title, Tone::Black, Align::Center, true);
  if (text && *text) {
    c.textWrapped({r.x + GUTTER * 2, y + th + 8, r.w - GUTTER * 4, r.h / 2}, font::BODY, text, Tone::Dark, 3);
  }
}

void dialog(Canvas& c, const char* title, const char* message, const char* okLabel, const uint16_t okAction,
            const char* cancelLabel, const uint16_t cancelAction) {
  c.hit({0, 0, c.width(), c.height()}, cancelAction);
  const int w = c.width() - 2 * GUTTER;
  const int h = 300;
  const Rect card{GUTTER, (c.height() - h) / 2, w, h};
  // Heavier shadow so the card reads as floating above the screen.
  c.fill(card.offset(SHADOW * 2, SHADOW * 2), Tone::Black);
  c.fill(card, Tone::White);
  c.frame(card, BORDER, Tone::Black);
  Rect inner = card.inset(GUTTER + BORDER);
  c.textIn(inner.takeTop(c.lineHeight(font::TITLE)), font::TITLE, title, Tone::Black, Align::Left, true);
  inner.takeTop(GAP);
  const Rect buttons = inner.takeBottom(MIN_TARGET + SHADOW);
  c.textWrapped(inner, font::BODY, message, Tone::Black, 4);
  const int bw = (buttons.w - GAP) / 2;
  button(c, {buttons.x, buttons.y, bw, buttons.h}, cancelLabel, cancelAction);
  button(c, {buttons.x + bw + GAP, buttons.y, bw, buttons.h}, okLabel, okAction, ButtonStyle::Primary);
}

}  // namespace fos::ui
