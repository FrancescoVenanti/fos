#include "AppLauncher.h"

#include <Logging.h>

#include "activities/ActivityManager.h"
#include "fos/launcher/LauncherActivity.h"
#include "fos/settings/SettingsActivity.h"
#include "fos/ui/FosActivity.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace fos {
namespace {

// Stand-in for an app that is not built yet.
class PlaceholderActivity final : public ui::FosActivity {
 public:
  PlaceholderActivity(GfxRenderer& r, MappedInputManager& in, const AppId id)
      : FosActivity("Placeholder", r, in), id_(id) {}

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override {
    ui::Rect area = body;
    ui::header(c, area.takeTop(ui::theme::HEADER_H), appInfo(id_).name, BACK);
    ui::emptyState(c, area, "Coming soon", "This app is part of a later FOs phase.");
  }
  void onAction(const uint16_t action, int16_t) override {
    if (action == BACK) onBack();
  }

 private:
  enum : uint16_t { BACK = 1 };
  AppId id_;
};

}  // namespace

void openApp(const AppId id) {
  LOG_DBG("FOS", "Open app %s", appInfo(id).name);
  std::unique_ptr<Activity> app;
  switch (id) {
    case AppId::Settings:
      app = std::make_unique<SettingsActivity>(renderer, mappedInputManager);
      break;
    default:
      app = std::make_unique<PlaceholderActivity>(renderer, mappedInputManager, id);
      break;
  }
  activityManager.pushActivity(std::move(app));
}

std::unique_ptr<Activity> makeLauncher(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<LauncherActivity>(renderer, mappedInput);
}

}  // namespace fos
