#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"

namespace fos::terminal {

// Terminal (docs/PRODUCT.md): a live client for a Claude Code session on the
// computer, through the relay (tools/terminal-relay) or the backend, which
// share one API: GET /api/v1/terminal/session?after=<seq>, POST
// /api/v1/terminal/prompt, POST /api/v1/terminal/approval. It needs a network
// and polls while open. The transcript is monospace and scrolls with Up/Down;
// quick replies and the keyboard send prompts; a permission request shows
// full-width Approve and Deny.
class TerminalActivity final : public ui::FosActivity {
 public:
  TerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum Action : uint16_t { BACK = 1, CHIP, TYPE, APPROVE, DENY, RETRY };
  enum class Kind : uint8_t { System, User, Assistant, Tool };

  struct Line {
    Kind kind;
    std::string text;
  };
  struct Permission {
    std::string id;
    std::string tool;
    std::string detail;
  };

  bool poll();
  bool post(const char* path, const std::string& json);
  void send(const std::string& text);
  void wrap(ui::Canvas& c, int width);
  int rowsPerPage(const ui::Canvas& c) const;
  ui::Rect transcriptRect() const;

  std::vector<Line> lines_;
  uint32_t lastSeq_ = 0;
  bool hasPermission_ = false;
  Permission permission_;
  std::string sessionName_;
  bool connected_ = false;
  bool connecting_ = true;
  std::string error_;
  bool wifiWasOn_ = false;
  unsigned long nextPollAt_ = 0;
  bool waiting_ = false;  // a prompt was sent; poll faster until the reply

  // Wrapped display rows, rebuilt when lines change or the width is known.
  struct Row {
    Kind kind;
    std::string text;
  };
  std::vector<Row> rows_;
  bool wrapped_ = false;
  int scrollTop_ = 0;  // first row shown
  int perPage_ = 10;   // rows that fit, measured while drawing
  bool followBottom_ = true;
};

}  // namespace fos::terminal
