#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "fos/ui/Branding.h"

void BootActivity::onEnter() {
  Activity::onEnter();
  // FOs: branded boot splash.
  fos::ui::drawBootScreen(renderer, "Starting");
}
