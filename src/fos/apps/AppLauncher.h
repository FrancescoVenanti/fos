#pragma once

#include <memory>

#include "AppRegistry.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace fos {

// Boot-time setup of the apps (sync pushers, status counters). Runs before
// the sync worker starts.
void initApps();

// Opens an app on top of the launcher (Back returns to the launcher).
void openApp(AppId id);

// The FOs home screen; ActivityManager::goHome() shows this.
std::unique_ptr<Activity> makeLauncher(GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace fos
