#pragma once

#include <string>
#include <vector>

#include "BudgetStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::budget {

// Budgeteer home (docs/PRODUCT.md). Opening it sends the queue and downloads
// the month. Offline it shows only "+ New transaction" and the queue counter;
// online it shows the month: summary, spent vs. budget, and the transaction
// list, with arrows to change month.
class BudgeteerActivity final : public ui::FosActivity {
 public:
  BudgeteerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum class State : uint8_t { Checking, Offline, Online };
  enum class Tab : uint8_t { Summary, Budgets, List };

  enum Action : uint16_t {
    BACK = 1,
    ADD,
    RETRY,
    PREV_MONTH,
    NEXT_MONTH,
    TAB,
    PREV_PAGE,
    NEXT_PAGE,
  };

  // Display-ready row, formatted when data loads (not while drawing).
  struct Row {
    std::string title;
    std::string detail;
    std::string value;
    long long spent = 0;
    long long budget = 0;
  };

  void connect();
  void reload();
  void buildRows();
  // Content area below the month row and tabs, above the main action.
  ui::Rect contentRect() const;
  // Rows per page of the current tab and whether a pager is needed.
  int perPage(bool& paged) const;
  int pageCount() const;
  const char* categoryName(const std::string& id) const;
  bool isCurrentMonth() const;

  void drawOffline(ui::Canvas& c, ui::Rect area);
  void drawSummary(ui::Canvas& c, ui::Rect area);
  void drawBudgets(ui::Canvas& c, ui::Rect area);
  void drawList(ui::Canvas& c, ui::Rect area);
  void drawPager(ui::Canvas& c, const ui::Rect& area);

  State state_ = State::Checking;
  Tab tab_ = Tab::Summary;
  int year_ = 0;
  int mon_ = 0;  // 0-based
  char ym_[8] = "";
  char monthTitle_[32] = "";
  uint32_t seenGen_ = 0;  // sync generation already loaded

  std::vector<Category> categories_;
  Month month_;
  int queued_ = 0;

  std::string balanceText_;
  std::string incomeText_;
  std::string expenseText_;
  char queuedText_[48] = "";
  std::vector<Row> budgetRows_;
  std::vector<Row> listRows_;
  int page_ = 0;
};

}  // namespace fos::budget
