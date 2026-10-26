#include "AppRegistry.h"

namespace fos {
namespace {

constexpr AppInfo APPS[APP_COUNT] = {
    {AppId::Budgeteer, "Budgeteer", AppGroup::MoneyPlanning, true, false},
    {AppId::Todo, "To-do", AppGroup::MoneyPlanning, false, false},
    {AppId::Calendar, "Calendar", AppGroup::MoneyPlanning, true, false},
    {AppId::Notes, "Notes", AppGroup::MoneyPlanning, false, false},
    {AppId::Reader, "Reader", AppGroup::ReadingLearning, false, false},
    {AppId::ReadLater, "Read-later", AppGroup::ReadingLearning, true, false},
    {AppId::Flashcards, "Flashcards", AppGroup::ReadingLearning, true, false},
    {AppId::Chess, "Chess", AppGroup::Play, true, false},
    {AppId::Puzzles, "Puzzles", AppGroup::Play, true, false},
    {AppId::Timers, "Timers", AppGroup::Utilities, false, false},
    {AppId::Calculator, "Calculator", AppGroup::Utilities, true, false},
    {AppId::Wallet, "Wallet", AppGroup::Utilities, true, false},
    {AppId::Habits, "Habits", AppGroup::Utilities, false, false},
    {AppId::Terminal, "Terminal", AppGroup::Utilities, true, true},
    {AppId::LockScreen, "Lock screen", AppGroup::System, true, false},
    {AppId::Settings, "Settings", AppGroup::System, false, false},
};

}  // namespace

const AppInfo& appInfo(const AppId id) {
  const int index = static_cast<int>(id);
  return APPS[index >= 0 && index < APP_COUNT ? index : APP_COUNT - 1];
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
