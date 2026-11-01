#include "AppRegistry.h"

namespace fos {
namespace {

constexpr AppInfo APPS[APP_COUNT] = {
    // id, name, group, usesNetwork, needsNetwork, retired
    {AppId::Budgeteer, "Budgeteer", AppGroup::MoneyPlanning, false, false, true},
    {AppId::Todo, "To-do", AppGroup::MoneyPlanning, false, false, false},
    {AppId::Calendar, "Calendar", AppGroup::MoneyPlanning, false, false, true},
    {AppId::Notes, "Notes", AppGroup::MoneyPlanning, false, false, false},
    {AppId::Reader, "Reader", AppGroup::ReadingLearning, false, false, false},
    {AppId::ReadLater, "Read-later", AppGroup::ReadingLearning, false, false, true},
    {AppId::Flashcards, "Flashcards", AppGroup::ReadingLearning, false, false, true},
    {AppId::Chess, "Chess", AppGroup::Play, true, false, false},
    {AppId::Puzzles, "Puzzles", AppGroup::Play, false, false, true},
    {AppId::Timers, "Timers", AppGroup::Utilities, false, false, false},
    {AppId::Calculator, "Calculator", AppGroup::Utilities, false, false, false},
    {AppId::Wallet, "Wallet", AppGroup::Utilities, false, false, false},
    {AppId::Habits, "Habits", AppGroup::Utilities, false, false, false},
    {AppId::Terminal, "Terminal", AppGroup::Utilities, true, true, false},
    {AppId::LockScreen, "Lock screen", AppGroup::System, true, false, false},
    {AppId::Settings, "Settings", AppGroup::System, false, false, false},
};

}  // namespace

const AppInfo& appInfo(const AppId id) {
  const int index = static_cast<int>(id);
  return APPS[index >= 0 && index < APP_COUNT ? index : APP_COUNT - 1];
}

bool isAvailable(const AppId id) {
  const int index = static_cast<int>(id);
  return index >= 0 && index < APP_COUNT && !APPS[index].retired;
}

const char* groupName(const AppGroup group) {
  switch (group) {
    case AppGroup::MoneyPlanning:
      return "Money and planning";
    case AppGroup::ReadingLearning:
      return "Reading and learning";
    case AppGroup::Play:
      return "Play";
    case AppGroup::Utilities:
      return "Utilities";
    case AppGroup::System:
      return "System";
  }
  return "";
}

}  // namespace fos
