#pragma once

#include <string>

#include "Canvas.h"
#include "Components.h"
#include "activities/Activity.h"

namespace fos::ui {

// Base class for FOs screens.
//
// Subclasses draw the whole body in draw(), which may run up to three times
// per frame (B/W base, then the two gray planes) and must not change state.
// Hit areas registered while drawing are routed to onAction() from the main
// loop. Touch-down on a registered area shows it pressed (no shadow, shifted)
// with a fast B/W refresh; the action fires on release.
//
// Physical buttons: Up/Down call onUp()/onDown() (swapped by Settings >
// Buttons), a short OK click calls onOk() (Back by default), two quick OK
// clicks go Home. The left-edge swipe also calls onBack().
class FosActivity : public Activity {
 public:
  FosActivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  virtual void draw(Canvas& c, const Rect& body) = 0;
  virtual void onAction(uint16_t action, int16_t value) {}
  // Long press on a registered area. Return true if handled.
  virtual bool onLongPress(uint16_t action, int16_t value) { return false; }
  virtual bool onUp() { return false; }
  virtual bool onDown() { return false; }
  virtual bool onOk();
  virtual void onBack();
  // Called every loop iteration (timers, polling).
  virtual void onTick() {}
  virtual bool showStatusBar() const { return true; }
  // Actions that should not show the pressed state (e.g. drawing surfaces).
  virtual bool pressable(uint16_t action) const { return true; }

  // Redraw on the next loop. full = clean full refresh (new screen, big change).
  void refresh(bool full = false);
  void goHome();
  // Body below the status bar.
  Rect bodyRect() const;

 private:
  // Draws one pass; returns whether any gray tone was used.
  bool renderPass(Pass pass, HitList* hits);

  HitList drawHits_;
  HitList liveHits_;
  uint16_t pressedAction_ = 0;
  int16_t pressedValue_ = 0;
  bool pressFrame_ = false;  // next frame only shows a press: fast B/W refresh
  bool fullNext_ = true;     // next frame is a full refresh
  int updatesSinceFull_ = 0;
};

}  // namespace fos::ui
