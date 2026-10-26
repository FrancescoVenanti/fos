#include "FosActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "fos/settings/FosSettings.h"

namespace fos::ui {
namespace {

// Two OK clicks closer than this go Home. Shared by all FOs screens, because
// the first click usually closes the screen that saw it.
constexpr unsigned long DOUBLE_OK_MS = 450;
unsigned long lastOkClickAt = 0;

StatusInfo frameStatus;

}  // namespace

FosActivity::FosActivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(std::move(name), renderer, mappedInput) {}

void FosActivity::onEnter() {
  Activity::onEnter();
  fullNext_ = true;
  requestUpdate();
}

Rect FosActivity::bodyRect() const {
  const int top = showStatusBar() ? theme::STATUS_BAR_H : 0;
  return {0, top, renderer.getScreenWidth(), renderer.getScreenHeight() - top};
}

void FosActivity::refresh(const bool full) {
  if (full) fullNext_ = true;
  requestUpdate();
}

void FosActivity::goHome() { activityManager.goHome(); }

bool FosActivity::onOk() {
  switch (settings().okAction) {
    case OkAction::Back:
      onBack();
      return true;
    case OkAction::Home:
      goHome();
      return true;
    case OkAction::Nothing:
      return false;
  }
  return false;
}

void FosActivity::onBack() { finish(); }

void FosActivity::loop() {
  // Activity state is read by the render task; mutate it under the render lock.
  RenderLock lock(*this);
  onTick();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }
  const bool swap = settings().swapUpDown;
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (swap ? onDown() : onUp()) requestUpdate();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (swap ? onUp() : onDown()) requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const unsigned long now = millis();
    if (lastOkClickAt != 0 && now - lastOkClickAt < DOUBLE_OK_MS) {
      lastOkClickAt = 0;
      goHome();
      return;
    }
    lastOkClickAt = now;
    onOk();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) {
    const Hit* hit = liveHits_.find(x, y);
    pressedAction_ = 0;
    if (hit && onLongPress(hit->action, hit->value)) {
      refresh();
      return;
    }
  }
  if (mappedInput.wasScreenTouchPressed(x, y)) {
    const Hit* hit = liveHits_.find(x, y);
    if (hit && pressable(hit->action)) {
      pressedAction_ = hit->action;
      pressedValue_ = hit->value;
      pressFrame_ = true;
      requestUpdate();
    }
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const Hit* hit = liveHits_.find(x, y);
    const bool hadPress = pressedAction_ != 0;
    pressedAction_ = 0;
    pressFrame_ = false;
    if (hit) {
      const Hit tapped = *hit;
      onAction(tapped.action, tapped.value);
      requestUpdate();
    } else if (hadPress) {
      requestUpdate();
    }
  } else if (mappedInput.wasScreenTouchReleased() && pressedAction_ != 0) {
    // Released after a swipe or off target: drop the pressed look.
    pressedAction_ = 0;
    requestUpdate();
  }
}

bool FosActivity::renderPass(const Pass pass, HitList* hits) {
  Canvas c(renderer, pass, settings().invertColors, hits, pressedAction_, pressedValue_);
  c.fill({0, 0, c.width(), c.height()}, Tone::White);
  if (showStatusBar()) statusBar(c, frameStatus);
  draw(c, bodyRect());
  return c.usedGray();
}

void FosActivity::render(RenderLock&&) {
  const bool pressOnly = pressFrame_;
  pressFrame_ = false;
  frameStatus = currentStatus();

  drawHits_.clear();
  renderer.clearScreen();
  const bool gray = renderPass(Pass::Bw, &drawHits_);
  liveHits_ = drawHits_;

  // Refresh policy (docs/DECISIONS.md D5): a press is a fast B/W frame; a new
  // screen, or every N updates, gets a cleaning half refresh.
  HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
  const int every = settings().fullRefreshEvery;
  if (!pressOnly && (fullNext_ || (every > 0 && updatesSinceFull_ >= every))) {
    mode = HalDisplay::HALF_REFRESH;
    updatesSinceFull_ = 0;
    fullNext_ = false;
  } else {
    updatesSinceFull_++;
  }

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (!gray || pressOnly || !absolute || !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, mode)) {
    renderer.displayBuffer(mode);
    return;
  }
  renderer.clearScreen(0xFF);
  renderPass(Pass::GrayLsb, nullptr);
  renderer.copyGrayscaleLsbBuffers();
  renderer.clearScreen(0xFF);
  renderPass(Pass::GrayMsb, nullptr);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  // Leave the B/W image in the framebuffer as the base for later partial refreshes.
  renderer.clearScreen();
  renderPass(Pass::Bw, nullptr);
  renderer.cleanupGrayscaleWithFrameBuffer();
}

}  // namespace fos::ui
