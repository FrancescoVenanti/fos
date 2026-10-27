#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::puzzles {

// Five-letter word puzzle: six guesses, each letter marked as in the right
// place, elsewhere in the word, or absent. Answers come from the bundled pack
// (one a day, offline) or the daily word that arrives with the sync. Guesses
// must be in the bundled dictionary (answers plus extra common words).

constexpr int WORD_LEN = 5;
constexpr int MAX_GUESSES = 6;

enum class Mark : uint8_t { Unknown = 0, Absent, Present, Correct };

// Marks for a guess against the answer (both upper case), counting repeated
// letters correctly.
void score(const std::string& guess, const std::string& answer, Mark out[WORD_LEN]);

bool isWord(const std::string& upper);
// The bundled answer for a day number (rotates through the pack).
std::string packWord(int day);
int packSize();

}  // namespace fos::puzzles
