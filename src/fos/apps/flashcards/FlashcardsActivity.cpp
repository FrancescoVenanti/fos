#include "FlashcardsActivity.h"

#include <cstdio>

#include "fos/sync/Sync.h"

namespace fos::cards {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr int ROW = 84;
constexpr const char* GRADES[] = {"Again", "Hard", "Good", "Easy"};

std::string intervalText(const int days) {
  if (days <= 0) return "today";
  if (days == 1) return "1 day";
  if (days < 30) return std::to_string(days) + " days";
  if (days < 365) return std::to_string((days + 15) / 30) + " mo";
  char out[16];
  snprintf(out, sizeof(out), "%.1f y", days / 365.0);
  return out;
}

}  // namespace

// --- decks ----------------------------------------------------------------------

FlashcardsActivity::FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Flashcards", renderer, mappedInput), scheduler_(std::make_shared<Scheduler>()) {}

void FlashcardsActivity::onEnter() {
  scheduler_->load();
  reload();
  seenGen_ = sync::generation();
  sync::request(AppId::Flashcards);
  FosActivity::onEnter();
}

void FlashcardsActivity::reload() {
  decks_ = loadDecks();
  const int today = todayNumber();
  details_.clear();
  totalDue_ = 0;
  for (const Deck& d : decks_) {
    const int due = scheduler_->dueCount(d, today);
    const int fresh = scheduler_->newCount(d, today);
    totalDue_ += due + fresh;
    char buf[80];
    snprintf(buf, sizeof(buf), "%d due  \xC2\xB7  %d new  \xC2\xB7  %d of %d learned", due, fresh,
             scheduler_->learnedCount(d), static_cast<int>(d.cards.size()));
    details_.emplace_back(buf);
  }
  const int reviewed = scheduler_->reviewedToday(today);
  char sum[64];
  if (totalDue_ == 0) {
    snprintf(sum, sizeof(sum), reviewed ? "All done for today (%d reviewed)" : "Nothing to review today", reviewed);
  } else {
    snprintf(sum, sizeof(sum), "%d to review today", totalDue_);
  }
  summary_ = sum;
}

void FlashcardsActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Flashcards", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  if (decks_.empty()) {
    emptyState(c, area, "No decks",
               "Decks arrive with the sync, or copy a CSV file (front,back per line) to the flashcards folder of the "
               "microSD.");
    return;
  }
  // Today's count, inverted when there is work to do.
  const Rect banner = area.takeTop(64);
  const bool work = totalDue_ > 0;
  if (work) c.fill(banner, Tone::Black);
  c.textIn(banner.inset(GUTTER, 0), font::TITLE, summary_.c_str(), work ? Tone::White : Tone::Black, Align::Left, true);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  for (size_t i = 0; i < decks_.size() && area.h >= ROW; ++i) {
    listRow(c, area.takeTop(ROW), decks_[i].name.c_str(), details_[i].c_str(), nullptr, OPEN, static_cast<int16_t>(i));
  }
}

void FlashcardsActivity::onAction(const uint16_t action, const int16_t value) {
  if (action == BACK) {
    onBack();
    return;
  }
  if (action == OPEN && value >= 0 && value < static_cast<int>(decks_.size())) {
    startActivityForResult(std::make_unique<ReviewActivity>(renderer, mappedInput, decks_[value], scheduler_),
                           [this](const ActivityResult&) { reload(); });
  }
}

void FlashcardsActivity::onTick() {
  const uint32_t gen = sync::generation();
  if (gen == seenGen_ || sync::busy()) return;
  seenGen_ = gen;
  reload();
  refresh();
}

// --- review ---------------------------------------------------------------------

ReviewActivity::ReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Deck deck,
                               std::shared_ptr<Scheduler> scheduler)
    : FosActivity("Review", renderer, mappedInput), deck_(std::move(deck)), scheduler_(std::move(scheduler)) {}

void ReviewActivity::onEnter() {
  today_ = todayNumber();
  queue_ = scheduler_->queue(deck_, today_);
  updateTexts();
  FosActivity::onEnter();
}

void ReviewActivity::updateTexts() {
  const size_t left = queue_.size() - done_;
  char buf[48];
  snprintf(buf, sizeof(buf), "%d left", static_cast<int>(left));
  progress_ = buf;
  if (done_ < queue_.size()) {
    const Card& card = deck_.cards[queue_[done_]];
    for (int g = 0; g < 4; ++g) {
      intervals_[g] = intervalText(scheduler_->previewInterval(deck_, card, static_cast<Grade>(g)));
    }
  }
}

void ReviewActivity::answer(const Grade g) {
  if (done_ >= queue_.size()) return;
  const int index = queue_[done_];
  scheduler_->grade(deck_, deck_.cards[index], g, today_);
  scheduler_->save();
  // Again: the card comes back at the end of this session.
  if (g == Grade::Again) queue_.push_back(index);
  ++done_;
  showing_ = false;
  updateTexts();
  refresh(true);
}

void ReviewActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  const Rect head = area.takeTop(HEADER_H);
  // The count on the right, the deck name in what is left.
  const int pw = c.textWidth(font::BODY, progress_.c_str(), true) + GUTTER;
  header(c, {head.x, head.y, head.w - pw, head.h}, deck_.name.c_str(), BACK);
  c.textIn({head.right() - pw, head.y, pw - GUTTER, head.h}, font::BODY, progress_.c_str(), Tone::Dark, Align::Right,
           true);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  area = area.inset(GUTTER, 0);
  if (done_ >= queue_.size()) {
    emptyState(c, area, queue_.empty() ? "Nothing due" : "Session done",
               queue_.empty() ? "Come back tomorrow: new cards and reviews arrive every day."
                              : "Every card for today is reviewed. The next ones come back when they are due.");
    return;
  }
  const Card& card = deck_.cards[queue_[done_]];
  const Rect actions = area.takeBottom(ACTION_H + GUTTER);
  area.takeBottom(GAP);

  // The card: front on top; the answer, inverted, below once shown.
  const int half = (area.h - GAP) / 2;
  const Rect front = block(c, {area.x, area.y, area.w, half}, Tone::White);
  c.textWrapped(front.inset(16, 16), font::TITLE, card.front.c_str(), Tone::Black, 6, true);
  const Rect backRect{area.x, area.y + half + GAP, area.w, half};
  if (showing_) {
    c.fill(backRect, Tone::Black);
    c.textWrapped(backRect.inset(BORDER + 16, 16), font::TITLE, card.back.c_str(), Tone::White, 6, true);
  } else {
    c.fill(backRect, Tone::Light);
    c.frame(backRect, BORDER, Tone::Black);
    c.textIn(backRect, font::BODY, "?", Tone::Dark, Align::Center, true);
  }

  const Rect a{actions.x, actions.y, actions.w, ACTION_H};
  if (!showing_) {
    button(c, a, "Show answer", SHOW, ButtonStyle::Primary, 0, font::TITLE);
    return;
  }
  const int bw = (a.w - 3 * 8) / 4;
  for (int g = 0; g < 4; ++g) {
    const Rect r{a.x + g * (bw + 8), a.y, bw, a.h};
    const Rect face = block(c, r,
                            g == 2   ? Tone::Black
                            : g == 0 ? Tone::Light
                                     : Tone::White,
                            GRADE, static_cast<int16_t>(g));
    const Tone ink = g == 2 ? Tone::White : Tone::Black;
    const int lh = c.lineHeight(font::BODY);
    const int sh = c.lineHeight(font::SMALL);
    const int top = face.y + (face.h - lh - sh) / 2;
    c.textIn({face.x, top, face.w, lh}, font::BODY, GRADES[g], ink, Align::Center, true);
    c.textIn({face.x, top + lh, face.w, sh}, font::SMALL, intervals_[g].c_str(), ink, Align::Center);
  }
}

void ReviewActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case SHOW:
      showing_ = true;
      return;
    case GRADE:
      if (value >= 0 && value < 4) answer(static_cast<Grade>(value));
      return;
    default:
      return;
  }
}

bool ReviewActivity::onOk() {
  if (done_ < queue_.size() && !showing_) {
    showing_ = true;
    requestUpdate();
    return true;
  }
  return FosActivity::onOk();
}

bool ReviewActivity::onUp() {
  if (!showing_) return false;
  answer(Grade::Again);
  return false;  // answer() already refreshed
}

bool ReviewActivity::onDown() {
  if (!showing_) return false;
  answer(Grade::Good);
  return false;
}

}  // namespace fos::cards
