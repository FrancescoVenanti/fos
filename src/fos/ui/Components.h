#pragma once

#include "Canvas.h"
#include "Fonts.h"
#include "Theme.h"
#include "fos/core/Status.h"

namespace fos::ui {

enum class ButtonStyle : uint8_t {
  Primary,    // black face, white label: the one main action of a screen
  Secondary,  // white face, black label
  Quiet,      // light gray face, black label, no shadow
};

// Small drawn glyphs used by buttons and rows.
enum class Glyph : uint8_t { None, Back, Forward, Up, Down, Plus, Minus, Check, Close, Menu, Play, Pause, Backspace };

// Status bar across the top of every screen (time, sync, Wi-Fi, battery).
void statusBar(Canvas& c, const StatusInfo& status);

// Screen title row. With backAction != 0 a back button sits on the left.
// rightLabel/rightAction add one small action on the right.
void header(Canvas& c, const Rect& r, const char* title, uint16_t backAction, const char* rightLabel = nullptr,
            uint16_t rightAction = 0, Glyph rightGlyph = Glyph::None);

// Raised block: fill, 3 px outline and 4 px hard shadow inside r. When
// (action, value) is pressed the face shifts onto the shadow. Registers r as a
// hit area when action != 0. Returns the face rect (inside the outline).
Rect block(Canvas& c, const Rect& r, Tone face, uint16_t action = 0, int16_t value = 0, bool shadow = true);

void button(Canvas& c, const Rect& r, const char* label, uint16_t action, ButtonStyle style = ButtonStyle::Secondary,
            int16_t value = 0, int fontId = font::BODY);
void glyphButton(Canvas& c, const Rect& r, Glyph glyph, uint16_t action, ButtonStyle style = ButtonStyle::Secondary,
                 int16_t value = 0);
void drawGlyph(Canvas& c, const Rect& r, Glyph glyph, Tone tone);

// List row: title, optional detail line, optional trailing value, chevron.
// Rows are separated by 3 px rules rather than shadows.
void listRow(Canvas& c, const Rect& r, const char* title, const char* detail, const char* trailing, uint16_t action,
             int16_t value = 0, bool chevron = true);
void sectionLabel(Canvas& c, const Rect& r, const char* text);

void toggle(Canvas& c, const Rect& track, bool on);
void toggleRow(Canvas& c, const Rect& r, const char* label, const char* detail, bool on, uint16_t action,
               int16_t value = 0);

// Segmented control; selected segment is inverted. Tapping fires
// (action, index).
void segmented(Canvas& c, const Rect& r, const char* const* labels, int count, int selected, uint16_t action);

// Chip; returns its width. Selected chips are black with white text.
int chip(Canvas& c, int x, int y, const char* label, bool selected, uint16_t action, int16_t value = 0);
int chipWidth(Canvas& c, const char* label);

void progressBar(Canvas& c, const Rect& r, int value, int max, Tone fill = Tone::Dark);
void rule(Canvas& c, int x, int y, int w, int thickness = theme::BORDER, Tone tone = Tone::Black);

// "‹  2 / 3  ›" pager; prev/next fire prevAction / nextAction.
void pager(Canvas& c, const Rect& r, int page, int pages, uint16_t prevAction, uint16_t nextAction);

void emptyState(Canvas& c, const Rect& r, const char* title, const char* text);

// Modal card centered on screen. Taps outside it fire cancelAction. Draw it
// last so its hit areas sit on top of the screen's.
void dialog(Canvas& c, const char* title, const char* message, const char* okLabel, uint16_t okAction,
            const char* cancelLabel, uint16_t cancelAction);

}  // namespace fos::ui
