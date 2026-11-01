#pragma once

#include <cstdint>

namespace fos {

// The FOs apps. The order is the default launcher order; values are persisted
// in settings, so append only. Removed apps keep their value and are marked
// retired (AppInfo::retired), so saved layouts keep their meaning.
enum class AppId : uint8_t {
  Budgeteer = 0,
  Todo,
  Calendar,
  Notes,
  Reader,
  ReadLater,
  Flashcards,
  Chess,
  Puzzles,
  Timers,
  Calculator,
  Wallet,
  Habits,
  Terminal,
  LockScreen,
  Settings,
  Count
};

constexpr int APP_COUNT = static_cast<int>(AppId::Count);

enum class AppGroup : uint8_t { MoneyPlanning, ReadingLearning, Play, Utilities, System };

struct AppInfo {
  AppId id;
  const char* name;
  AppGroup group;
  bool usesNetwork;   // syncs or talks to a server when available
  bool needsNetwork;  // does not work offline (Terminal)
  bool retired;       // removed from FOs: never listed or opened
};

const AppInfo& appInfo(AppId id);
// False for retired apps.
bool isAvailable(AppId id);
const char* groupName(AppGroup group);

}  // namespace fos
