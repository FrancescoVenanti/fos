#include "NotesStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "fos/core/Json.h"
#include "fos/core/Status.h"

namespace fos::notes {
namespace {

constexpr char INDEX_FILE[] = "/fos/notes/index.json";

std::vector<Note> store;
uint32_t nextId = 1;
bool loaded = false;

std::string sketchPath(const uint32_t id) { return "/fos/notes/sketch-" + std::to_string(id) + ".json"; }

void load() {
  loaded = true;
  store.clear();
  JsonDocument doc;
  if (!loadJson(INDEX_FILE, doc)) return;
  nextId = doc["nextId"] | 1u;
  for (JsonObjectConst n : doc["notes"].as<JsonArrayConst>()) {
    Note note;
    note.id = n["id"] | 0u;
    note.kind = (n["kind"] | 0) == 1 ? Kind::Sketch : Kind::Text;
    note.title = n["title"] | "";
    note.text = n["text"] | "";
    note.updated = n["updated"] | 0u;
    if (note.id) store.push_back(std::move(note));
  }
  std::stable_sort(store.begin(), store.end(), [](const Note& a, const Note& b) { return a.updated > b.updated; });
}

}  // namespace

std::vector<Note>& notes() {
  if (!loaded) load();
  return store;
}

Note* find(const uint32_t id) {
  for (Note& n : notes()) {
    if (n.id == id) return &n;
  }
  return nullptr;
}

uint32_t create(const Kind kind) {
  notes();
  Note note;
  note.id = nextId++;
  note.kind = kind;
  note.title = kind == Kind::Sketch ? "Sketch" : "";
  note.updated = static_cast<uint32_t>(std::time(nullptr));
  store.insert(store.begin(), note);
  return note.id;
}

void remove(const uint32_t id) {
  auto& list = notes();
  const auto it = std::find_if(list.begin(), list.end(), [id](const Note& n) { return n.id == id; });
  if (it == list.end()) return;
  if (it->kind == Kind::Sketch) Storage.remove(sketchPath(id).c_str());
  list.erase(it);
}

void touch(Note& note) {
  note.updated = static_cast<uint32_t>(std::time(nullptr));
  if (note.kind == Kind::Text) {
    const size_t nl = note.text.find('\n');
    note.title = note.text.substr(0, std::min<size_t>(nl, 60));
  }
  auto& list = notes();
  const uint32_t id = note.id;
  std::stable_sort(list.begin(), list.end(), [id](const Note& a, const Note& b) {
    if ((a.id == id) != (b.id == id)) return a.id == id;
    return a.updated > b.updated;
  });
}

bool save() {
  JsonDocument doc;
  doc["nextId"] = nextId;
  JsonArray arr = doc["notes"].to<JsonArray>();
  for (const Note& n : notes()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = n.id;
    o["kind"] = static_cast<int>(n.kind);
    o["title"] = n.title;
    if (n.kind == Kind::Text) o["text"] = n.text;
    o["updated"] = n.updated;
  }
  const bool ok = saveJson(INDEX_FILE, doc);
  if (!ok) LOG_ERR("NOTES", "Cannot write %s", INDEX_FILE);
  return ok;
}

bool loadSketch(const uint32_t id, std::vector<Stroke>& out) {
  out.clear();
  JsonDocument doc;
  if (!loadJson(sketchPath(id).c_str(), doc)) return false;
  for (JsonObjectConst s : doc["strokes"].as<JsonArrayConst>()) {
    Stroke stroke;
    stroke.eraser = s["e"] | false;
    // Points are a flat array: x0, y0, x1, y1, ...
    JsonArrayConst pts = s["p"];
    stroke.points.reserve(pts.size() / 2);
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
      stroke.points.push_back({static_cast<int16_t>(pts[i] | 0), static_cast<int16_t>(pts[i + 1] | 0)});
    }
    out.push_back(std::move(stroke));
  }
  return true;
}

bool saveSketch(const uint32_t id, const std::vector<Stroke>& strokes) {
  JsonDocument doc;
  JsonArray arr = doc["strokes"].to<JsonArray>();
  for (const Stroke& s : strokes) {
    JsonObject o = arr.add<JsonObject>();
    if (s.eraser) o["e"] = true;
    JsonArray pts = o["p"].to<JsonArray>();
    for (const Point& p : s.points) {
      pts.add(p.x);
      pts.add(p.y);
    }
  }
  return saveJson(sketchPath(id).c_str(), doc);
}

std::string describeTime(const uint32_t unix) {
  if (unix < 1700000000) return "";
  const std::time_t t = unix;
  std::tm tm{};
  localtime_r(&t, &tm);
  char clock[12];
  formatClock(tm, clock, sizeof(clock));
  char out[32];
  snprintf(out, sizeof(out), "%d %.3s, %s", tm.tm_mday, monthName(tm.tm_mon), clock);
  return out;
}

}  // namespace fos::notes
