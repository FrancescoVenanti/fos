#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::notes {

// Notes live only on the device in /fos/notes/: index.json lists them, a
// typed note's text is in its index entry, and a sketch's strokes are in
// sketch-<id>.json (points in canvas pixels, 0..CANVAS_W x 0..CANVAS_H).

constexpr int CANVAS_W = 448;
constexpr int CANVAS_H = 560;
constexpr size_t MAX_TEXT = 2000;
constexpr size_t MAX_POINTS = 6000;  // per sketch; drawing stops at the limit

enum class Kind : uint8_t { Text = 0, Sketch };

struct Note {
  uint32_t id = 0;
  Kind kind = Kind::Text;
  std::string title;  // text notes: their first line
  std::string text;
  uint32_t updated = 0;  // unix time
};

struct Point {
  int16_t x;
  int16_t y;
};

// A stroke is a polyline; width 3 (pen) or 18 (eraser, drawn white).
struct Stroke {
  bool eraser = false;
  std::vector<Point> points;
};

std::vector<Note>& notes();  // newest first; loaded on first use
Note* find(uint32_t id);
uint32_t create(Kind kind);
void remove(uint32_t id);
void touch(Note& note);  // bumps updated and moves it to the front
bool save();

bool loadSketch(uint32_t id, std::vector<Stroke>& out);
bool saveSketch(uint32_t id, const std::vector<Stroke>& strokes);

// "26 Oct, 14:05".
std::string describeTime(uint32_t unix);

}  // namespace fos::notes
