#pragma once

#include <string>
#include <vector>

#include "Barcodes.h"
#include "fos/ui/FosActivity.h"

namespace fos::wallet {

struct Code {
  std::string id;
  std::string label;
  std::string payload;
  Format format = Format::Qr;
};

// Wallet (docs/PRODUCT.md): loyalty cards, tickets, boarding passes, kept in
// /fos/wallet/codes.json: the codes added on the device, and codes copied to
// the microSD by hand in the same format ({"codes": [{"label", "payload",
// "format"}]}).
class WalletActivity final : public ui::FosActivity {
 public:
  WalletActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;

 private:
  enum Action : uint16_t { BACK = 1, OPEN, ADD, PREV_PAGE, NEXT_PAGE };

  void reload();
  int perPage() const;

  std::vector<Code> codes_;
  std::vector<std::string> details_;
  int page_ = 0;
};

// One code, full screen at full contrast. Up/Down go to the previous/next code.
class CodeActivity final : public ui::FosActivity {
 public:
  CodeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Code> codes, int index);

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  bool showStatusBar() const override { return false; }

 private:
  enum Action : uint16_t { BACK = 1, DELETE_CODE, DIALOG_OK, DIALOG_CANCEL };

  std::vector<Code> codes_;
  int index_;
  bool confirmDelete_ = false;
};

// Adds a code: label, the code's text, the format.
class AddCodeActivity final : public ui::FosActivity {
 public:
  AddCodeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;

 private:
  enum Action : uint16_t { BACK = 1, LABEL, PAYLOAD, FORMAT, SAVE };

  void editField(bool label);

  std::string label_;
  std::string payload_;
  int format_ = 0;
  std::string error_;
};

}  // namespace fos::wallet
