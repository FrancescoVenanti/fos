#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::todo {

// To-do data, kept only on the device in /fos/todo/todo.json. Dates are
// "YYYY-MM-DD" in local time; an empty due date means "no date".

enum class Repeat : uint8_t { None = 0, Daily, Weekly, Monthly };

struct List {
  uint32_t id = 0;
  std::string name;
};

struct Task {
  uint32_t id = 0;
  uint32_t list = 0;
  std::string title;
  std::string due;
  Repeat repeat = Repeat::None;
  bool done = false;
  std::string doneOn;  // date it was completed
};

constexpr size_t MAX_LISTS = 6;
constexpr size_t MAX_LIST_NAME = 16;
constexpr size_t MAX_TITLE = 80;

struct Data {
  std::vector<List> lists;
  std::vector<Task> tasks;
  uint32_t nextId = 1;

  const List* list(uint32_t id) const;
  Task* task(uint32_t id);
  // New task at the end of its list; returns its id.
  uint32_t addTask(uint32_t list, std::string title);
  // False when the list limit is reached.
  bool addList(std::string name);
  // Removes a list and its tasks. The last list cannot be removed.
  bool removeList(uint32_t id);
  void removeTask(uint32_t id);
  // Tap-to-complete. A recurring task is not marked done: its due date moves
  // to the next occurrence after today. A done task is reopened.
  void toggleDone(uint32_t id);
  // Removes completed tasks (of one list, or all with list == 0).
  int clearDone(uint32_t list);
};

// Loaded on first use; a missing file gives one empty "Inbox" list.
Data& data();
bool save();

// Date helpers ("YYYY-MM-DD").
std::string today();
std::string addDays(const std::string& date, int days);
std::string nextOccurrence(const std::string& due, Repeat repeat);
// "Today", "Tomorrow", "Yesterday", or "Thu 29 Oct".
std::string describeDate(const std::string& date);
const char* repeatName(Repeat repeat);

}  // namespace fos::todo
