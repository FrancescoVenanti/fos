#include "TextEntryActivity.h"

namespace fos::ui {

using namespace theme;

TextEntryActivity::TextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                     std::string text, const bool secret, const size_t maxLength, std::string hint)
    : FosActivity("TextEntry", renderer, mappedInput),
      title_(std::move(title)),
      hint_(std::move(hint)),
      secret_(secret),
      keyboard_(std::move(text), maxLength) {}

void TextEntryActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), title_.c_str(), BACK, "Done", DONE);

  // Text field: white box, 3 px outline, the value in the body font with a
  // block cursor. Secret values show dots except the last character.
  area = area.inset(GUTTER, 0);
  const Rect field{area.x, area.y + 8, area.w, 120};
  c.fill(field, Tone::White);
  c.frame(field, BORDER, Tone::Black);
  const Rect inside = field.inset(BORDER + 10, BORDER + 8);

  std::string shown = keyboard_.text();
  if (secret_ && !shown.empty()) {
    std::string masked;
    for (size_t i = 0; i + 1 < shown.size(); ++i) masked += '*';
    masked += shown.back();
    shown = masked;
  }
  shown += "_";
  const int used = c.textWrapped(inside, font::BODY, shown.c_str(), Tone::Black, 3, true);
  (void)used;

  if (!keyboard_.text().empty()) {
    button(c, {field.right() - 104, field.bottom() + GAP, 104, 52}, "Clear", CLEAR, ButtonStyle::Secondary, 0,
           font::SMALL);
  }
  if (!hint_.empty()) {
    c.textWrapped({area.x, field.bottom() + GAP + 8, area.w - 120, 60}, font::SMALL, hint_.c_str(), Tone::Dark, 3);
  }

  const int kh = keyboard_.height();
  keyboard_.draw(c, {GUTTER / 2, body.bottom() - kh - GUTTER / 2, c.width() - GUTTER, kh}, KEY);
}

void TextEntryActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      break;
    case DONE:
      done();
      break;
    case CLEAR:
      keyboard_.setText({});
      break;
    case KEY:
      if (keyboard_.press(value) == Keyboard::Event::Enter) done();
      break;
    default:
      break;
  }
}

void TextEntryActivity::done() {
  setResult(KeyboardResult{keyboard_.text()});
  finish();
}

void TextEntryActivity::onBack() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}

}  // namespace fos::ui
