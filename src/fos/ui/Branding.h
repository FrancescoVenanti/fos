#pragma once

class GfxRenderer;

namespace fos::ui {

// Boot splash: the FOs wordmark card, a status line and the version.
void drawBootScreen(const GfxRenderer& renderer, const char* status);
// Default sleep screen until the Lock screen app takes over idle.
void drawSleepScreen(const GfxRenderer& renderer);

}  // namespace fos::ui
