#pragma once

#include "LockScreen.h"
#include "fos/ui/FosActivity.h"

namespace fos::lock {

// The Lock screen app: the same screen as the sleep frame, kept current while
// open (every minute, or every second with the seconds clock). A tap or OK
// closes it. Opening it asks the sync worker for fresh weather.
class LockScreenActivity final : public ui::FosActivity {
 public:
  LockScreenActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onTick() override;
  bool pressable(uint16_t) const override { return false; }

 private:
  enum Action : uint16_t { CLOSE = 1 };

  Snapshot snapshot_;
  int shownSecond_ = -1;
  int shownMinute_ = -1;
  uint32_t seenGen_ = 0;
};

}  // namespace fos::lock
