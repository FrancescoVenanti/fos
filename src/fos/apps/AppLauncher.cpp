#include "AppLauncher.h"

#include <Logging.h>

#include "activities/ActivityManager.h"
#include "fos/apps/budgeteer/BudgetStore.h"
#include "fos/apps/budgeteer/BudgeteerActivity.h"
#include "fos/apps/calculator/CalculatorActivity.h"
#include "fos/apps/calendar/CalendarActivity.h"
#include "fos/apps/chess/ChessActivity.h"
#include "fos/apps/lock/LockScreenActivity.h"
#include "fos/apps/reader/ReaderHomeActivity.h"
#include "fos/apps/timers/TimerStore.h"
#include "fos/apps/timers/TimersActivity.h"
#include "fos/apps/todo/TodoActivity.h"
#include "fos/launcher/LauncherActivity.h"
#include "fos/settings/SettingsActivity.h"
#include "fos/sync/Direct.h"
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

void initApps() {
  sync::registerDirectSources();
  budget::init();
  // Loads the timers and raises the status-bar alert for one that ended
  // while the device was off or asleep.
  timers::update();
}

void openApp(const AppId id) {
  LOG_DBG("FOS", "Open app %s", appInfo(id).name);
  std::unique_ptr<Activity> app;
  switch (id) {
    case AppId::Budgeteer:
      app = std::make_unique<budget::BudgeteerActivity>(renderer, mappedInputManager);
      break;
    case AppId::Todo:
      app = std::make_unique<todo::TodoActivity>(renderer, mappedInputManager);
      break;
    case AppId::Calendar:
      app = std::make_unique<calendar::CalendarActivity>(renderer, mappedInputManager);
      break;
    case AppId::LockScreen:
      app = std::make_unique<lock::LockScreenActivity>(renderer, mappedInputManager);
      break;
    case AppId::Timers:
      app = std::make_unique<timers::TimersActivity>(renderer, mappedInputManager);
      break;
    case AppId::Reader:
      app = std::make_unique<reader::ReaderHomeActivity>(renderer, mappedInputManager);
      break;
    case AppId::Chess:
      app = std::make_unique<chess::ChessActivity>(renderer, mappedInputManager);
      break;
    case AppId::Calculator:
      app = std::make_unique<calc::CalculatorActivity>(renderer, mappedInputManager);
      break;
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
