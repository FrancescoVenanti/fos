#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::cards {

// Flashcard decks come from the sync (/fos/flashcards/cache.json) and from CSV
// files copied to /fos/flashcards/ ("front,back" per line, the file name is
// the deck name), so decks work without a backend. Review state is kept per
// card in /fos/flashcards/progress.json.

struct Card {
  std::string id;  // stable: the synced id, or a hash of the front for CSV cards
  std::string front;
  std::string back;
};

struct Deck {
  std::string id;
  std::string name;
  std::vector<Card> cards;
};

// Scheduling state of one card (SM-2 style).
struct Progress {
  int32_t due = 0;        // day number (days since 1970-01-01, local)
  uint16_t interval = 0;  // days
  uint16_t ease = 250;    // percent
  uint16_t reps = 0;      // successful reviews in a row
  uint16_t lapses = 0;
  bool seen = false;
};

enum class Grade : uint8_t { Again = 0, Hard, Good, Easy };

constexpr int NEW_PER_DAY = 10;  // new cards introduced per deck per day

std::vector<Deck> loadDecks();
// Parses one CSV line ("front,back"; quotes for commas inside a field).
bool parseCsvLine(const std::string& line, std::string& front, std::string& back);

class Scheduler {
 public:
  void load();
  bool save() const;

  // Today's queue for a deck: due cards (oldest first), then new cards up to
  // the daily limit. Indexes into deck.cards.
  std::vector<int> queue(const Deck& deck, int today) const;
  int dueCount(const Deck& deck, int today) const;
  int newCount(const Deck& deck, int today) const;
  int learnedCount(const Deck& deck) const;
  // Applies an answer. Again keeps the card in today's queue (due today).
  void grade(const Deck& deck, const Card& card, Grade g, int today);
  // "in 4 days" for the button labels.
  int previewInterval(const Deck& deck, const Card& card, Grade g) const;

  // Reviews done today, for the summary.
  int reviewedToday(int today) const { return today == reviewDay_ ? reviewedToday_ : 0; }

 private:
  struct Entry {
    std::string key;  // deckId + "/" + cardId
    Progress p;
  };
  const Progress* find(const std::string& key) const;
  Progress& get(const std::string& key);
  int newIntroduced(const Deck& deck, int today) const;

  std::vector<Entry> entries_;
  // New cards started per deck today: "deckId" -> count, reset each day.
  std::vector<std::pair<std::string, int>> newToday_;
  int newDay_ = 0;
  int reviewDay_ = 0;
  int reviewedToday_ = 0;
};

// Days since 1970-01-01 in local time (0 when the clock is not set).
int todayNumber();

}  // namespace fos::cards
