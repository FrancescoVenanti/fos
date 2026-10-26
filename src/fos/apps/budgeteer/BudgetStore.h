#pragma once

#include <string>
#include <vector>

namespace fos::budget {

// Budgeteer data on the microSD (/fos/budgeteer/):
//   cache-categories.json  categories, downloaded by the sync worker
//   cache-month-YYYY-MM.json  a month's totals, budgets and transactions
//   queue.json  entries typed on the device, waiting for the backend
//   recent.json  the last descriptions per category (description chips)
// Amounts are integer cents, dates "YYYY-MM-DD".

struct Category {
  std::string id;
  std::string name;
  bool income = false;
  bool excluded = false;  // left out of the monthly balance (NoCat)
};

struct Entry {
  std::string clientId;  // set by enqueue(); the backend ignores repeats
  bool income = false;
  long long amount = 0;
  std::string category;
  std::string description;
  std::string date;
};

struct BudgetLine {
  std::string category;
  long long budget = 0;
  long long spent = 0;
};

struct Month {
  bool loaded = false;
  long long income = 0;
  long long expense = 0;
  long long balance = 0;
  std::vector<BudgetLine> budgets;
  std::vector<Entry> transactions;  // newest first
};

// Registers the sync pusher and publishes the queue size to the status bar.
// Call once at boot, before the sync worker starts.
void init();

bool loadCategories(std::vector<Category>& out);
// ym is "YYYY-MM". False when that month was never downloaded.
bool loadMonth(const char* ym, Month& out);
// Queues a download of a month on the sync worker.
void requestMonth(const char* ym);

// Adds an entry to the queue (assigns its clientId) and remembers its
// description. The next sync sends it.
bool enqueue(Entry entry);
std::vector<Entry> queued();
int queuedCount();

// Up to `max` recent descriptions for a category, newest first: what was typed
// on the device, then what the month download contains.
std::vector<std::string> recentDescriptions(const std::string& category, const Month* month, size_t max = 5);

// "YYYY-MM" of a year and 0-based month.
void formatYm(int year, int mon, char* out, size_t size);

}  // namespace fos::budget
