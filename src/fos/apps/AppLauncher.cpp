#include "AppLauncher.h"

#include <Logging.h>

#include "activities/ActivityManager.h"
#include "fos/apps/budgeteer/BudgetStore.h"
#include "fos/apps/budgeteer/BudgeteerActivity.h"
#include "fos/apps/calculator/CalculatorActivity.h"
#include "fos/apps/calendar/CalendarActivity.h"
#include "fos/apps/chess/ChessActivity.h"
#include "fos/apps/flashcards/FlashcardsActivity.h"
#include "fos/apps/habits/HabitsActivity.h"
#include "fos/apps/lock/LockScreenActivity.h"
#include "fos/apps/notes/NotesActivity.h"
#include "fos/apps/puzzles/PuzzlesActivity.h"
#include "fos/apps/reader/ReaderHomeActivity.h"
#include "fos/apps/readlater/ReadLaterActivity.h"
#include "fos/apps/terminal/TerminalActivity.h"
#include "fos/apps/timers/TimerStore.h"
#include "fos/apps/timers/TimersActivity.h"
#include "fos/apps/todo/TodoActivity.h"
#include "fos/apps/wallet/WalletActivity.h"
#include "fos/launcher/LauncherActivity.h"
#include "fos/settings/SettingsActivity.h"
#include "fos/sync/Direct.h"
#include "fos/ui/FosActivity.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace fos {
void initApps() {
  sync::registerDirectSources();
  budget::init();
  readlater::init();
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
    case AppId::Notes:
      app = std::make_unique<notes::NotesActivity>(renderer, mappedInputManager);
      break;
    case AppId::Habits:
      app = std::make_unique<habits::HabitsActivity>(renderer, mappedInputManager);
      break;
    case AppId::Wallet:
      app = std::make_unique<wallet::WalletActivity>(renderer, mappedInputManager);
      break;
    case AppId::Flashcards:
      app = std::make_unique<cards::FlashcardsActivity>(renderer, mappedInputManager);
      break;
    case AppId::ReadLater:
      app = std::make_unique<readlater::ReadLaterActivity>(renderer, mappedInputManager);
      break;
    case AppId::Puzzles:
      app = std::make_unique<puzzles::PuzzlesActivity>(renderer, mappedInputManager);
      break;
    case AppId::Terminal:
      app = std::make_unique<terminal::TerminalActivity>(renderer, mappedInputManager);
      break;
    case AppId::Settings:
      app = std::make_unique<SettingsActivity>(renderer, mappedInputManager);
      break;
    case AppId::Count:
      break;
  }
  if (app) activityManager.pushActivity(std::move(app));
}

std::unique_ptr<Activity> makeLauncher(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<LauncherActivity>(renderer, mappedInput);
}

}  // namespace fos
