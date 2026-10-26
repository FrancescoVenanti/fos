#include "LockScreenActivity.h"

#include "fos/core/Status.h"
#include "fos/settings/FosSettings.h"
#include "fos/sync/Sync.h"

namespace fos::lock {

LockScreenActivity::LockScreenActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("LockScreen", renderer, mappedInput) {}

void LockScreenActivity::onEnter() {
  capture(snapshot_);
  shownMinute_ = snapshot_.time.tm_min;
  shownSecond_ = snapshot_.time.tm_sec;
  seenGen_ = sync::generation();
  sync::request(AppId::LockScreen);
  FosActivity::onEnter();
}

void LockScreenActivity::draw(ui::Canvas& c, const ui::Rect& body) {
  lock::draw(c, body, snapshot_, false);
  c.hit(body, CLOSE);
}

void LockScreenActivity::onAction(const uint16_t action, int16_t) {
  if (action == CLOSE) finish();
}

void LockScreenActivity::onTick() {
  std::tm now{};
  const bool hasTime = localNow(now);
  const bool seconds = settings().clockStyle == ClockStyle::WithSeconds;
  const uint32_t gen = sync::generation();
  const bool synced = gen != seenGen_ && !sync::busy();
  if (synced) seenGen_ = gen;
  const bool tick = hasTime && (now.tm_min != shownMinute_ || (seconds && now.tm_sec != shownSecond_));
  if (!tick && !synced) return;
  if (tick && seconds && now.tm_min == shownMinute_) {
    // Only the seconds moved: keep the rest and skip the sensor read.
    snapshot_.time = now;
  } else {
    capture(snapshot_);
  }
  shownMinute_ = now.tm_min;
  shownSecond_ = now.tm_sec;
  // A new minute is a normal update; the full-refresh interval cleans ghosting.
  refresh();
}

}  // namespace fos::lock
