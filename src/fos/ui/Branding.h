#pragma once

class GfxRenderer;

namespace fos::ui {

// Boot splash: the FOs wordmark card, a status line and the version.
void drawBootScreen(const GfxRenderer& renderer, const char* status);

}  // namespace fos::ui
