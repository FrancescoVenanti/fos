#pragma once

#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"

namespace fos::reader {

// Reader (docs/PRODUCT.md): the front page of CrossPoint's e-reader. The last
// book is one tap away ("Continue reading"), then the other recent books, the
// library (books with covers) and the file browser. The reader itself, its
// menus, bookmarks and settings are CrossPoint's, unchanged; leaving them
// returns to the launcher.
class ReaderHomeActivity final : public ui::FosActivity {
 public:
  ReaderHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;

 private:
  enum Action : uint16_t { BACK = 1, OPEN, LIBRARY, FILES };

  struct Book {
    std::string path;
    std::string title;
    std::string detail;
  };

  std::vector<Book> books_;
};

}  // namespace fos::reader
