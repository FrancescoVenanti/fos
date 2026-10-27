#include "Deck.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/sync/Sync.h"

namespace fos::cards {
namespace {

constexpr char FOLDER[] = "/fos/flashcards";
constexpr char PROGRESS_FILE[] = "/fos/flashcards/progress.json";
constexpr size_t MAX_CARDS_PER_DECK = 2000;

std::string hashId(const std::string& text) {
  uint32_t h = 2166136261u;
  for (const unsigned char ch : text) {
    h ^= ch;
    h *= 16777619u;
  }
  char out[12];
  snprintf(out, sizeof(out), "h%08lx", static_cast<unsigned long>(h));
  return out;
}

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void loadCsv(const std::string& path, const std::string& name, std::vector<Deck>& out) {
  HalFile f = Storage.open(path.c_str());
  if (!f) return;
  Deck deck;
  deck.id = "csv-" + name;
  deck.name = name;
  std::string line;
  const auto flush = [&]() {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string front;
    std::string back;
    if (parseCsvLine(line, front, back) && deck.cards.size() < MAX_CARDS_PER_DECK) {
      deck.cards.push_back({hashId(front), front, back});
    }
    line.clear();
  };
  char buf[256];
  int n = 0;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; ++i) {
      if (buf[i] == '\n') {
        flush();
      } else if (line.size() < 1024) {
        line += buf[i];
      }
    }
  }
  flush();
  f.close();
  // A header row "front,back" is not a card.
  if (!deck.cards.empty() && deck.cards[0].front == "front" && deck.cards[0].back == "back") {
    deck.cards.erase(deck.cards.begin());
  }
  if (!deck.cards.empty()) out.push_back(std::move(deck));
}

}  // namespace

int todayNumber() {
  std::tm t{};
  if (!localNow(t)) return 0;
  // Days from civil date (local).
  int y = t.tm_year + 1900;
  const unsigned m = t.tm_mon + 1;
  const unsigned d = t.tm_mday;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

bool parseCsvLine(const std::string& line, std::string& front, std::string& back) {
  std::vector<std::string> fields(1);
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quoted) {
      if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
        fields.back() += '"';
        ++i;
      } else if (ch == '"') {
        quoted = false;
      } else {
        fields.back() += ch;
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == ',' || ch == ';' || ch == '\t') {
      fields.emplace_back();
    } else {
      fields.back() += ch;
    }
  }
  if (fields.size() < 2) return false;
  const auto trim = [](std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
  };
  front = trim(fields[0]);
  back = trim(fields[1]);
  return !front.empty() && !back.empty();
}

std::vector<Deck> loadDecks() {
  std::vector<Deck> out;
  JsonDocument doc;
  if (loadJson(sync::cachePath(AppId::Flashcards).c_str(), doc)) {
    for (JsonObjectConst d : doc["data"]["decks"].as<JsonArrayConst>()) {
      Deck deck;
      deck.id = d["id"] | "";
      deck.name = d["name"] | deck.id.c_str();
      for (JsonObjectConst c : d["cards"].as<JsonArrayConst>()) {
        Card card;
        card.front = c["front"] | "";
        card.back = c["back"] | "";
        card.id = c["id"] | hashId(card.front).c_str();
        if (!card.front.empty()) deck.cards.push_back(std::move(card));
      }
      if (!deck.id.empty() && !deck.cards.empty()) out.push_back(std::move(deck));
    }
  }
  HalFile dir = Storage.open(FOLDER);
  if (dir && dir.isDirectory()) {
    std::vector<std::string> names;
    while (HalFile f = dir.openNextFile()) {
      char name[128];
      f.getName(name, sizeof(name));
      const std::string n = name;
      if (!f.isDirectory() && (endsWith(n, ".csv") || endsWith(n, ".CSV"))) names.push_back(n);
      f.close();
    }
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) loadCsv(std::string(FOLDER) + "/" + n, n.substr(0, n.size() - 4), out);
  }
  if (dir) dir.close();
  return out;
}

void Scheduler::load() {
  entries_.clear();
  newToday_.clear();
  JsonDocument doc;
  if (!loadJson(PROGRESS_FILE, doc)) return;
  for (JsonPairConst kv : doc["cards"].as<JsonObjectConst>()) {
    JsonArrayConst a = kv.value();  // [due, interval, ease, reps, lapses]
    Entry e;
    e.key = kv.key().c_str();
    e.p.due = a[0] | 0;
    e.p.interval = a[1] | 0;
    e.p.ease = a[2] | 250;
    e.p.reps = a[3] | 0;
    e.p.lapses = a[4] | 0;
    e.p.seen = true;
    entries_.push_back(std::move(e));
  }
  newDay_ = doc["newDay"] | 0;
  for (JsonPairConst kv : doc["newToday"].as<JsonObjectConst>())
    newToday_.emplace_back(kv.key().c_str(), kv.value() | 0);
  reviewDay_ = doc["reviewDay"] | 0;
  reviewedToday_ = doc["reviewed"] | 0;
}

bool Scheduler::save() const {
  JsonDocument doc;
  JsonObject cards = doc["cards"].to<JsonObject>();
  for (const Entry& e : entries_) {
    JsonArray a = cards[e.key].to<JsonArray>();
    a.add(e.p.due);
    a.add(e.p.interval);
    a.add(e.p.ease);
    a.add(e.p.reps);
    a.add(e.p.lapses);
  }
  doc["newDay"] = newDay_;
  JsonObject nt = doc["newToday"].to<JsonObject>();
  for (const auto& kv : newToday_) nt[kv.first] = kv.second;
  doc["reviewDay"] = reviewDay_;
  doc["reviewed"] = reviewedToday_;
  return saveJson(PROGRESS_FILE, doc);
}

const Progress* Scheduler::find(const std::string& key) const {
  for (const Entry& e : entries_) {
    if (e.key == key) return &e.p;
  }
  return nullptr;
}

Progress& Scheduler::get(const std::string& key) {
  for (Entry& e : entries_) {
    if (e.key == key) return e.p;
  }
  entries_.push_back({key, Progress()});
  return entries_.back().p;
}

int Scheduler::newIntroduced(const Deck& deck, const int today) const {
  if (today != newDay_) return 0;
  for (const auto& kv : newToday_) {
    if (kv.first == deck.id) return kv.second;
  }
  return 0;
}

std::vector<int> Scheduler::queue(const Deck& deck, const int today) const {
  std::vector<std::pair<int, int>> due;  // (due day, index)
  std::vector<int> fresh;
  for (int i = 0; i < static_cast<int>(deck.cards.size()); ++i) {
    const Progress* p = find(deck.id + "/" + deck.cards[i].id);
    if (!p) {
      fresh.push_back(i);
    } else if (p->due <= today) {
      due.emplace_back(p->due, i);
    }
  }
  std::stable_sort(due.begin(), due.end());
  std::vector<int> out;
  out.reserve(due.size() + NEW_PER_DAY);
  for (const auto& d : due) out.push_back(d.second);
  const int room = std::max(0, NEW_PER_DAY - newIntroduced(deck, today));
  for (int i = 0; i < std::min(room, static_cast<int>(fresh.size())); ++i) out.push_back(fresh[i]);
  return out;
}

int Scheduler::dueCount(const Deck& deck, const int today) const {
  int n = 0;
  for (const Card& c : deck.cards) {
    const Progress* p = find(deck.id + "/" + c.id);
    if (p && p->due <= today) ++n;
  }
  return n;
}

int Scheduler::newCount(const Deck& deck, const int today) const {
  int fresh = 0;
  for (const Card& c : deck.cards) fresh += find(deck.id + "/" + c.id) ? 0 : 1;
  return std::min(fresh, std::max(0, NEW_PER_DAY - newIntroduced(deck, today)));
}

int Scheduler::learnedCount(const Deck& deck) const {
  int n = 0;
  for (const Card& c : deck.cards) {
    const Progress* p = find(deck.id + "/" + c.id);
    if (p && p->interval >= 21) ++n;
  }
  return n;
}

namespace {

// SM-2 with four buttons: returns the new interval and updates ease/reps.
int nextInterval(Progress p, const Grade g, Progress* out) {
  if (g == Grade::Again) {
    p.reps = 0;
    if (p.seen && p.interval > 0) ++p.lapses;
    p.ease = static_cast<uint16_t>(std::max(130, p.ease - 20));
    p.interval = 0;  // again today
  } else {
    if (g == Grade::Hard) p.ease = static_cast<uint16_t>(std::max(130, p.ease - 15));
    if (g == Grade::Easy) p.ease = static_cast<uint16_t>(p.ease + 15);
    int interval;
    if (p.reps == 0) {
      interval = g == Grade::Easy ? 4 : 1;
    } else if (p.reps == 1) {
      interval = g == Grade::Hard ? 3 : g == Grade::Easy ? 8 : 6;
    } else {
      const double factor = g == Grade::Hard ? 1.2 : p.ease / 100.0 * (g == Grade::Easy ? 1.3 : 1.0);
      interval = static_cast<int>(p.interval * factor + 0.5);
      interval = std::max(interval, p.interval + 1);
    }
    p.interval = static_cast<uint16_t>(std::min(interval, 3650));
    ++p.reps;
  }
  p.seen = true;
  if (out) *out = p;
  return p.interval;
}

}  // namespace

int Scheduler::previewInterval(const Deck& deck, const Card& card, const Grade g) const {
  const Progress* p = find(deck.id + "/" + card.id);
  return nextInterval(p ? *p : Progress(), g, nullptr);
}

void Scheduler::grade(const Deck& deck, const Card& card, const Grade g, const int today) {
  const std::string key = deck.id + "/" + card.id;
  const bool isNew = find(key) == nullptr;
  Progress& p = get(key);
  nextInterval(p, g, &p);
  p.due = today + p.interval;
  if (isNew) {
    if (newDay_ != today) {
      newDay_ = today;
      newToday_.clear();
    }
    bool found = false;
    for (auto& kv : newToday_) {
      if (kv.first == deck.id) {
        ++kv.second;
        found = true;
      }
    }
    if (!found) newToday_.emplace_back(deck.id, 1);
  }
  if (reviewDay_ != today) {
    reviewDay_ = today;
    reviewedToday_ = 0;
  }
  ++reviewedToday_;
}

}  // namespace fos::cards
