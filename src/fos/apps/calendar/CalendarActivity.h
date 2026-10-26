#pragma once

#include <string>
#include <vector>

#include "CalendarStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::calendar {

// Calendar (docs/PRODUCT.md): a read-only agenda for today and the next 6
// days from the synced copy. Opening it asks for a fresh copy; offline it
// shows the last one and says how old it is. The next event is inverted.
class CalendarActivity final : public ui::FosActivity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  static constexpr int DAYS = 7;

  enum Action : uint16_t { BACK = 1, PREV_PAGE, NEXT_PAGE, REFRESH };
  enum class Kind : uint8_t { Day, Event, Empty };

  // Display-ready line of the agenda, built when data or the minute changes.
  struct Item {
    Kind kind = Kind::Event;
    std::string time;    // Day: "TODAY"; Event: start or "All day"
    std::string time2;   // Event: end
    std::string title;   // Day: "Mon 26 Oct"
    std::string detail;  // Event: location
    bool past = false;
    bool next = false;
    bool now = false;  // running right now
  };

  void reload();
  void rebuild();
  ui::Rect listRect() const;
  static int itemHeight(Kind kind);
  void drawItem(ui::Canvas& c, const ui::Rect& r, const Item& item) const;

  Agenda agenda_;
  std::vector<Item> items_;
  std::vector<int> pageStarts_;
  int page_ = 0;
  std::string status_;
  std::string stamp_;  // minute the items were built for
  uint32_t seenGen_ = 0;
  bool online_ = false;
  bool checked_ = false;  // a sync finished since the screen opened
  bool busy_ = false;
};

}  // namespace fos::calendar
