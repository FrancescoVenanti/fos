#include "TerminalActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "fos/settings/FosSettings.h"
#include "fos/sync/Backend.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos::terminal {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* CHIPS[] = {"continue", "run the tests", "explain"};
constexpr size_t MAX_LINES = 300;
constexpr unsigned long POLL_MS = 3000;
constexpr unsigned long FAST_POLL_MS = 1200;
constexpr int PERMISSION_H = 190;
constexpr int INPUT_H = CHIP_H + GAP + MIN_TARGET + SHADOW + GUTTER;

std::string baseUrl() {
  const FosSettings& s = settings();
  std::string url = s.terminalUrl.empty() ? s.backendUrl : s.terminalUrl;
  if (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

std::string bearer() {
  const FosSettings& s = settings();
  return s.terminalUrl.empty() ? s.backendKey : s.terminalKey;
}

const char* prefix(const uint8_t kind) {
  switch (kind) {
    case 1:
      return "> ";  // user
    case 3:
      return "$ ";  // tool
    case 0:
      return "# ";  // system
    default:
      return "";
  }
}

}  // namespace

TerminalActivity::TerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Terminal", renderer, mappedInput) {}

void TerminalActivity::onEnter() {
  wifiWasOn_ = WiFi.status() == WL_CONNECTED;
  connecting_ = true;
  nextPollAt_ = millis() + 200;  // the "Connecting" frame goes out first
  FosActivity::onEnter();
}

void TerminalActivity::onExit() {
  if (!wifiWasOn_) net::wifiOff();
  FosActivity::onExit();
}

bool TerminalActivity::post(const char* path, const std::string& json) {
  const net::Response r = net::request("POST", baseUrl() + path, json, bearer(), 10000);
  if (!r.ok()) {
    error_ = r.status < 0 ? "Cannot reach the relay." : "The relay answered " + std::to_string(r.status) + ".";
    return false;
  }
  return true;
}

bool TerminalActivity::poll() {
  if (baseUrl().empty()) {
    error_ =
        "No relay set. Start tools/terminal-relay/relay.py on your computer and enter its address in Settings "
        "> Accounts.";
    return false;
  }
  if (!net::ensureWifi()) {
    error_ = "No Wi-Fi. The terminal needs a connection.";
    return false;
  }
  const net::Response r =
      net::request("GET", baseUrl() + "/api/v1/terminal/session?after=" + std::to_string(lastSeq_), "", bearer(), 8000);
  JsonDocument doc;
  if (!r.ok() || deserializeJson(doc, r.body)) {
    error_ = r.status == 401 ? "The relay did not accept the key." : "Cannot reach the relay.";
    return false;
  }
  error_.clear();
  sessionName_ = doc["session"] | "";
  bool changed = false;
  for (JsonObjectConst l : doc["lines"].as<JsonArrayConst>()) {
    const uint32_t seq = l["seq"] | 0u;
    if (seq <= lastSeq_) continue;
    lastSeq_ = seq;
    const char* kind = l["kind"] | "assistant";
    Kind k = Kind::Assistant;
    if (strcmp(kind, "user") == 0) k = Kind::User;
    if (strcmp(kind, "tool") == 0) k = Kind::Tool;
    if (strcmp(kind, "system") == 0) k = Kind::System;
    lines_.push_back({k, l["text"] | ""});
    if (k == Kind::Assistant) waiting_ = false;
    changed = true;
  }
  if (lines_.size() > MAX_LINES) lines_.erase(lines_.begin(), lines_.begin() + (lines_.size() - MAX_LINES));
  JsonObjectConst p = doc["permission"];
  const bool had = hasPermission_;
  hasPermission_ = !p.isNull();
  if (hasPermission_) {
    const std::string id = p["id"] | "";
    changed = changed || id != permission_.id;
    permission_ = {id, p["tool"] | "", p["detail"] | ""};
    waiting_ = false;
  }
  if (changed || had != hasPermission_) wrapped_ = false;
  return true;
}

void TerminalActivity::send(const std::string& text) {
  if (text.empty() || !connected_) return;
  JsonDocument doc;
  doc["text"] = text;
  std::string body;
  serializeJson(doc, body);
  if (post("/api/v1/terminal/prompt", body)) {
    waiting_ = true;
    followBottom_ = true;
    nextPollAt_ = millis() + 300;
  }
}

void TerminalActivity::onTick() {
  if (millis() < nextPollAt_) return;
  const bool ok = poll();
  const bool wasConnected = connected_;
  connected_ = ok;
  connecting_ = false;
  nextPollAt_ = millis() + (waiting_ ? FAST_POLL_MS : POLL_MS);
  if (!wrapped_ || wasConnected != connected_) refresh(!wasConnected && connected_);
}

Rect TerminalActivity::transcriptRect() const {
  Rect area = bodyRect();
  area.takeTop(HEADER_H + BORDER);
  area.takeBottom(hasPermission_ ? PERMISSION_H : INPUT_H);
  return area.inset(GUTTER, 6);
}

int TerminalActivity::rowsPerPage(const Canvas& c) const {
  return std::max(1, transcriptRect().h / (c.lineHeight(font::MONO) + 2));
}

void TerminalActivity::wrap(Canvas& c, const int width) {
  rows_.clear();
  // Monospace: the column count comes from one character's width.
  const int charW = std::max(1, c.textWidth(font::MONO, "M"));
  const size_t cols = static_cast<size_t>(std::max(10, width / charW));
  for (const Line& l : lines_) {
    // A tool line that already shows its own prompt keeps it.
    const bool ownPrompt = l.kind == Kind::Tool && l.text.compare(0, 2, "$ ") == 0;
    std::string text = (ownPrompt ? "" : prefix(static_cast<uint8_t>(l.kind))) + l.text;
    size_t start = 0;
    while (start <= text.size()) {
      size_t nl = text.find('\n', start);
      if (nl == std::string::npos) nl = text.size();
      std::string para = text.substr(start, nl - start);
      if (para.empty()) rows_.push_back({l.kind, ""});
      while (!para.empty()) {
        size_t take = std::min(cols, para.size());
        if (take < para.size()) {
          const size_t space = para.rfind(' ', take);
          if (space != std::string::npos && space > cols / 2) take = space;
        }
        rows_.push_back({l.kind, para.substr(0, take)});
        para.erase(0, take);
        if (!para.empty() && para[0] == ' ') para.erase(0, 1);
      }
      start = nl + 1;
    }
    rows_.push_back({l.kind, ""});  // gap between messages
  }
  if (!rows_.empty()) rows_.pop_back();
  const int per = rowsPerPage(c);
  const int maxTop = std::max(0, static_cast<int>(rows_.size()) - per);
  if (followBottom_ || scrollTop_ > maxTop) scrollTop_ = maxTop;
  wrapped_ = true;
}

void TerminalActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  const std::string title = sessionName_.empty() ? "Terminal" : "Terminal  \xC2\xB7  " + sessionName_;
  header(c, area.takeTop(HEADER_H), title.c_str(), BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);

  if (connecting_) {
    emptyState(c, area, "Connecting...", "Joining Wi-Fi and asking the relay for the session.");
    return;
  }
  if (!connected_) {
    const Rect retry = area.takeBottom(MIN_TARGET + SHADOW + GUTTER).inset(GUTTER, 0);
    button(c, {retry.x, retry.y, retry.w, MIN_TARGET + SHADOW}, "Try again", RETRY);
    emptyState(c, area, "Not connected", error_.c_str());
    return;
  }

  // Transcript.
  const Rect t = transcriptRect();
  if (!wrapped_) wrap(c, t.w);
  const int lh = c.lineHeight(font::MONO) + 2;
  const int per = rowsPerPage(c);
  perPage_ = per;
  for (int i = scrollTop_; i < std::min(static_cast<int>(rows_.size()), scrollTop_ + per); ++i) {
    const Row& r = rows_[i];
    const Tone ink = r.kind == Kind::Assistant || r.kind == Kind::User ? Tone::Black : Tone::Dark;
    c.text(font::MONO, t.x, t.y + (i - scrollTop_) * lh, r.text.c_str(), ink, r.kind == Kind::User);
  }
  // Scroll position on the right edge.
  if (static_cast<int>(rows_.size()) > per) {
    const int barH = std::max(20, t.h * per / static_cast<int>(rows_.size()));
    const int barY = t.y + (t.h - barH) * scrollTop_ / std::max(1, static_cast<int>(rows_.size()) - per);
    c.fill({t.right() + 6, barY, 4, barH}, Tone::Black);
  }

  Rect bottom = body;
  if (hasPermission_) {
    // Permission request: inverted, two full-width buttons.
    Rect p = bottom.takeBottom(PERMISSION_H);
    c.fill(p, Tone::Black);
    p = p.inset(GUTTER, 10);
    const std::string head = "ALLOW " + permission_.tool + "?";
    c.textIn(p.takeTop(c.lineHeight(font::SMALL)), font::SMALL, head.c_str(), Tone::White, Align::Left, true);
    c.textIn(p.takeTop(c.lineHeight(font::MONO) + 6), font::MONO, permission_.detail.c_str(), Tone::White);
    p.takeTop(6);
    const int bw = (p.w - GAP) / 2;
    const Rect deny{p.x, p.y, bw, MIN_TARGET + SHADOW};
    const Rect approve{p.x + bw + GAP, p.y, bw, MIN_TARGET + SHADOW};
    button(c, deny, "Deny", DENY, ButtonStyle::Quiet, 0, font::TITLE);
    button(c, approve, "Approve", APPROVE, ButtonStyle::Secondary, 0, font::TITLE);
    return;
  }

  Rect input = bottom.takeBottom(INPUT_H);
  rule(c, 0, input.y, c.width());
  input = input.inset(GUTTER, 0);
  input.takeTop(BORDER + 6);
  int x = input.x;
  for (int i = 0; i < 3; ++i) x += chip(c, x, input.y, CHIPS[i], false, CHIP, static_cast<int16_t>(i)) + 8;
  input.takeTop(CHIP_H + GAP - 6);
  const char* label = waiting_ ? "Waiting for Claude..." : "Type a prompt";
  button(c, {input.x, input.y, input.w, MIN_TARGET + SHADOW}, label, TYPE,
         waiting_ ? ButtonStyle::Quiet : ButtonStyle::Primary);
}

void TerminalActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case RETRY:
      connecting_ = true;
      nextPollAt_ = millis() + 200;
      return;
    case CHIP:
      if (value >= 0 && value < 3) send(CHIPS[value]);
      return;
    case TYPE:
      startActivityForResult(std::make_unique<TextEntryActivity>(renderer, mappedInput, "Prompt", "", false, 400),
                             [this](const ActivityResult& result) {
                               const auto* text = std::get_if<KeyboardResult>(&result.data);
                               if (!result.isCancelled && text) send(text->text);
                             });
      return;
    case APPROVE:
    case DENY: {
      JsonDocument doc;
      doc["id"] = permission_.id;
      doc["approve"] = action == APPROVE;
      std::string body;
      serializeJson(doc, body);
      if (post("/api/v1/terminal/approval", body)) {
        hasPermission_ = false;
        wrapped_ = false;
        waiting_ = action == APPROVE;
        followBottom_ = true;
        nextPollAt_ = millis() + 300;
      }
      return;
    }
    default:
      return;
  }
}

// Up/Down scroll a page, keeping two rows of context.
bool TerminalActivity::onUp() {
  if (scrollTop_ == 0) return false;
  scrollTop_ = std::max(0, scrollTop_ - std::max(1, perPage_ - 2));
  followBottom_ = false;
  return true;
}

bool TerminalActivity::onDown() {
  const int maxTop = std::max(0, static_cast<int>(rows_.size()) - perPage_);
  if (scrollTop_ >= maxTop) return false;
  scrollTop_ = std::min(maxTop, scrollTop_ + std::max(1, perPage_ - 2));
  // Back at the end: new output scrolls into view again.
  followBottom_ = scrollTop_ >= maxTop;
  return true;
}

}  // namespace fos::terminal
