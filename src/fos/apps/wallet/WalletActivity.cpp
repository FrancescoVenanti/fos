#include "WalletActivity.h"

#include <ArduinoJson.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>

#include "fos/core/Json.h"
#include "fos/sync/Sync.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos::wallet {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char LOCAL_FILE[] = "/fos/wallet/codes.json";
constexpr int ROW = 80;
constexpr const char* FORMATS[] = {"QR", "Code 128", "EAN-13"};
constexpr Format FORMAT_VALUES[] = {Format::Qr, Format::Code128, Format::Ean13};

const char* formatKey(const Format f) {
  switch (f) {
    case Format::Code128:
      return "code128";
    case Format::Ean13:
      return "ean13";
    case Format::Qr:
      break;
  }
  return "qr";
}

void readCodes(const char* path, const bool synced, std::vector<Code>& out) {
  JsonDocument doc;
  if (!loadJson(path, doc)) return;
  JsonArrayConst list = synced ? doc["data"]["codes"].as<JsonArrayConst>() : doc["codes"].as<JsonArrayConst>();
  for (JsonObjectConst c : list) {
    Code code;
    code.id = c["id"] | "";
    code.label = c["label"] | "";
    code.payload = c["payload"] | "";
    code.format = parseFormat(c["kind"] | "", c["format"] | "");
    code.local = !synced;
    if (!code.payload.empty()) out.push_back(std::move(code));
  }
}

bool writeLocal(const std::vector<Code>& codes) {
  JsonDocument doc;
  JsonArray arr = doc["codes"].to<JsonArray>();
  for (const Code& c : codes) {
    if (!c.local) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c.id;
    o["label"] = c.label;
    o["payload"] = c.payload;
    o["format"] = formatKey(c.format);
  }
  return saveJson(LOCAL_FILE, doc);
}

}  // namespace

// --- list -----------------------------------------------------------------------

WalletActivity::WalletActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Wallet", renderer, mappedInput) {}

void WalletActivity::onEnter() {
  reload();
  seenGen_ = sync::generation();
  sync::request(AppId::Wallet);
  FosActivity::onEnter();
}

void WalletActivity::reload() {
  codes_.clear();
  readCodes(LOCAL_FILE, false, codes_);
  readCodes(sync::cachePath(AppId::Wallet).c_str(), true, codes_);
  details_.clear();
  for (const Code& c : codes_) {
    std::string d = formatName(c.format);
    d += "  \xC2\xB7  ";
    d += c.local ? "on this device" : "synced";
    details_.push_back(std::move(d));
  }
  const int pages = std::max(1, (static_cast<int>(codes_.size()) + perPage() - 1) / perPage());
  if (page_ >= pages) page_ = pages - 1;
}

int WalletActivity::perPage() const {
  const int h = bodyRect().h - HEADER_H - BORDER - (MIN_TARGET + SHADOW + GAP);
  return std::max(1, h / ROW);
}

void WalletActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Wallet", BACK, nullptr, ADD, Glyph::Plus);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  if (codes_.empty()) {
    emptyState(c, area, "No codes yet",
               "Tap + to add a loyalty card or ticket, or copy codes.json to the wallet folder of the microSD.");
    return;
  }
  const int per = perPage();
  const int pages = (static_cast<int>(codes_.size()) + per - 1) / per;
  if (pages > 1) {
    const Rect p = area.takeBottom(MIN_TARGET + SHADOW + GAP);
    pager(c, {p.x + GUTTER, p.y + GAP / 2, p.w - 2 * GUTTER, MIN_TARGET + SHADOW - GAP / 2}, page_, pages, PREV_PAGE,
          NEXT_PAGE);
  }
  for (int i = page_ * per; i < std::min(static_cast<int>(codes_.size()), (page_ + 1) * per); ++i) {
    listRow(c, area.takeTop(ROW), codes_[i].label.c_str(), details_[i].c_str(), nullptr, OPEN, static_cast<int16_t>(i));
  }
}

void WalletActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case OPEN:
      if (value >= 0 && value < static_cast<int>(codes_.size())) {
        startActivityForResult(std::make_unique<CodeActivity>(renderer, mappedInput, codes_, value),
                               [this](const ActivityResult&) { reload(); });
      }
      return;
    case ADD:
      startActivityForResult(std::make_unique<AddCodeActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { reload(); });
      return;
    case PREV_PAGE:
      onUp();
      return;
    case NEXT_PAGE:
      onDown();
      return;
    default:
      return;
  }
}

bool WalletActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool WalletActivity::onDown() {
  if ((page_ + 1) * perPage() >= static_cast<int>(codes_.size())) return false;
  ++page_;
  return true;
}

void WalletActivity::onTick() {
  const uint32_t gen = sync::generation();
  if (gen == seenGen_ || sync::busy()) return;
  seenGen_ = gen;
  reload();
  refresh();
}

// --- one code -------------------------------------------------------------------

CodeActivity::CodeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Code> codes,
                           const int index)
    : FosActivity("WalletCode", renderer, mappedInput), codes_(std::move(codes)), index_(index) {}

void CodeActivity::draw(Canvas& c, const Rect& body) {
  const Code& code = codes_[index_];
  Rect area = body;
  header(c, area.takeTop(HEADER_H), code.label.c_str(), BACK, code.local ? "Delete" : nullptr,
         code.local ? DELETE_CODE : 0);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  // Footer: position and format.
  const Rect footer = area.takeBottom(48).inset(GUTTER, 0);
  char pos[48];
  snprintf(pos, sizeof(pos), "%d / %d  \xC2\xB7  %s", index_ + 1, static_cast<int>(codes_.size()),
           formatName(code.format));
  c.textIn(footer, font::SMALL, pos, Tone::Dark, Align::Center, true);
  // The code fills the rest, on plain white.
  const Rect codeArea = area.inset(GUTTER, GUTTER);
  if (!drawCode(c, codeArea, code.format, code.payload)) {
    emptyState(c, codeArea, "Cannot draw this code", code.payload.c_str());
  }
  if (confirmDelete_) {
    dialog(c, "Delete code?", "It is removed from this device.", "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  }
}

void CodeActivity::onAction(const uint16_t action, int16_t) {
  if (confirmDelete_) {
    confirmDelete_ = false;
    if (action == DIALOG_OK) {
      codes_.erase(codes_.begin() + index_);
      writeLocal(codes_);
      finish();
      return;
    }
    refresh(true);
    return;
  }
  if (action == BACK) onBack();
  if (action == DELETE_CODE) confirmDelete_ = true;
}

bool CodeActivity::onUp() {
  if (index_ == 0) return false;
  --index_;
  refresh(true);
  return true;
}

bool CodeActivity::onDown() {
  if (index_ + 1 >= static_cast<int>(codes_.size())) return false;
  ++index_;
  refresh(true);
  return true;
}

// --- add ------------------------------------------------------------------------

AddCodeActivity::AddCodeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("WalletAdd", renderer, mappedInput) {}

void AddCodeActivity::editField(const bool label) {
  startActivityForResult(std::make_unique<TextEntryActivity>(
                             renderer, mappedInput, label ? "Name" : "Code", label ? label_ : payload_, false,
                             label ? 40 : 300, label ? "For example: Gym card" : "The number or text the code holds."),
                         [this, label](const ActivityResult& result) {
                           const auto* text = std::get_if<KeyboardResult>(&result.data);
                           if (result.isCancelled || !text) return;
                           (label ? label_ : payload_) = text->text;
                           error_.clear();
                         });
}

void AddCodeActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "New code", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  listRow(c, area.takeTop(ROW), "Name", label_.empty() ? "Not set" : label_.c_str(), nullptr, LABEL);
  listRow(c, area.takeTop(ROW), "Code", payload_.empty() ? "Not set" : payload_.c_str(), nullptr, PAYLOAD);
  area.takeTop(GAP);
  sectionLabel(c, area.takeTop(30), "FORMAT");
  segmented(c, area.takeTop(TAB_H).inset(GUTTER, 0), FORMATS, 3, format_, FORMAT);
  area.takeTop(GAP * 2);
  if (!error_.empty())
    c.textWrapped(area.takeTop(60).inset(GUTTER, 0), font::BODY, error_.c_str(), Tone::Black, 2, true);
  const Rect save = body.inset(GUTTER, 0);
  button(c, {save.x, save.bottom() - GUTTER - ACTION_H, save.w, ACTION_H}, "Save", SAVE, ButtonStyle::Primary, 0,
         font::TITLE);
}

void AddCodeActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case LABEL:
      editField(true);
      return;
    case PAYLOAD:
      editField(false);
      return;
    case FORMAT:
      format_ = value;
      error_.clear();
      return;
    case SAVE: {
      if (label_.empty() || payload_.empty()) {
        error_ = "Enter a name and the code.";
        return;
      }
      const Format f = FORMAT_VALUES[format_];
      std::vector<uint8_t> widths;
      std::string digits;
      if ((f == Format::Ean13 && !encodeEan13(payload_, widths, digits)) ||
          (f == Format::Code128 && !encodeCode128(payload_, widths))) {
        error_ = f == Format::Ean13 ? "EAN-13 needs 12 or 13 digits with a correct check digit."
                                    : "Code 128 takes letters, digits and symbols without accents.";
        return;
      }
      std::vector<Code> all;
      readCodes(LOCAL_FILE, false, all);
      char id[24];
      snprintf(id, sizeof(id), "local-%08lx", static_cast<unsigned long>(esp_random()));
      all.push_back({id, label_, payload_, f, true});
      writeLocal(all);
      finish();
      return;
    }
    default:
      return;
  }
}

}  // namespace fos::wallet
