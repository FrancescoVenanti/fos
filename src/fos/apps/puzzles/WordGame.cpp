#include "WordGame.h"

#include <cstring>

namespace fos::puzzles {
namespace {

// Bundled answers: common English five-letter words.
constexpr const char* ANSWERS[] = {
    "ABOUT", "ACTOR", "ADULT", "AGAIN", "AGREE", "ALARM", "ALBUM", "ALERT", "ALIVE", "ALLOW", "ALONE", "ANGLE", "APPLE",
    "APRON", "ARENA", "ARGUE", "ARMOR", "ARROW", "ASIDE", "AWARD", "BADGE", "BAKER", "BASIC", "BEACH", "BEARD", "BEAST",
    "BENCH", "BERRY", "BIRTH", "BLADE", "BLANK", "BLEND", "BLOCK", "BLOOM", "BOARD", "BRAIN", "BRAVE", "BREAD", "BRICK",
    "BRIDE", "BRISK", "BROOM", "BRUSH", "BUILD", "CABIN", "CABLE", "CAMEL", "CANDY", "CARGO", "CARRY", "CHAIN", "CHAIR",
    "CHALK", "CHARM", "CHART", "CHEST", "CHIEF", "CHILD", "CIDER", "CLAIM", "CLEAN", "CLEAR", "CLIMB", "CLOCK", "CLOUD",
    "COAST", "COCOA", "CORAL", "COUCH", "COUNT", "CRAFT", "CRANE", "CREEK", "CRISP", "CROWN", "DAISY", "DANCE", "DEPTH",
    "DIARY", "DOUGH", "DRAFT", "DREAM", "DRESS", "DRINK", "EAGLE", "EARTH", "EIGHT", "EMBER", "EMPTY", "ENJOY", "ENTRY",
    "EQUAL", "EXTRA", "FABLE", "FAITH", "FEAST", "FENCE", "FERRY", "FIELD", "FLAME", "FLASH", "FLEET", "FLOOR", "FLOUR",
    "FOCUS", "FORGE", "FRAME", "FRESH", "FROST", "FRUIT", "GHOST", "GIANT", "GLASS", "GLOBE", "GLOVE", "GRACE", "GRAIN",
    "GRAPE", "GRASS", "GREEN", "GUARD", "GUEST", "GUIDE", "HABIT", "HAPPY", "HEART", "HONEY", "HORSE", "HOTEL", "HOUSE",
    "IDEAL", "IMAGE", "INDEX", "INNER", "IVORY", "JELLY", "JEWEL", "JUICE", "KNIFE", "LABEL", "LASER", "LAUGH", "LAYER",
    "LEMON", "LEVEL", "LIGHT", "LINEN", "LOCAL", "LUCKY", "LUNCH", "MAGIC", "MAPLE", "MARCH", "MEDAL", "MERRY", "METAL",
    "MODEL", "MONEY", "MONTH", "MOUSE", "MUSIC", "NERVE", "NIGHT", "NOBLE", "NOISE", "NORTH", "NOVEL", "OCEAN", "OLIVE",
    "ONION", "OPERA", "ORBIT", "ORDER", "OTTER", "PAINT", "PANEL", "PAPER", "PARTY", "PEACH", "PEARL", "PEDAL", "PENNY",
    "PIANO", "PILOT", "PLANE", "PLANT", "PLATE", "POINT", "POLAR", "PRIZE", "PROOF", "PUPIL", "QUEEN", "QUIET", "RADIO",
    "RAVEN", "REACH", "RIVER", "ROBIN", "ROBOT", "ROUND", "ROYAL", "SALAD", "SCALE", "SCARF", "SCENE", "SHADE", "SHAPE",
    "SHARE", "SHELF", "SHELL", "SHINE", "SHIRT", "SKATE", "SLEEP", "SLICE", "SMILE", "SNACK", "SOLAR", "SOUND", "SPACE",
    "SPARK", "SPICE", "SPOON", "SPORT", "STAGE", "STAIR", "STAMP", "STEAM", "STONE", "STORM", "STORY", "STOVE", "SUGAR",
    "SUNNY", "SWEET", "TABLE", "TEACH", "THEME", "TIGER", "TOAST", "TODAY", "TORCH", "TOWER", "TRAIN", "TREAT", "TRUST",
    "TULIP", "UNCLE", "UNITY", "VALUE", "VAPOR", "VIDEO", "VISIT", "VOICE", "WAGON", "WATCH", "WATER", "WHALE", "WHEAT",
    "WHEEL", "WORLD", "YOUTH", "ZEBRA"};

// More common words accepted as guesses.
constexpr const char* EXTRA[] = {
    "ABOVE", "ADMIT", "AFTER", "ALIEN", "AMONG", "ANGRY", "ARISE", "AUDIO", "AVOID", "BEGIN", "BEING", "BELOW", "BIRDS",
    "BLACK", "BLIND", "BLOOD", "BLUES", "BONUS", "BOOST", "BOUND", "BREAK", "BRING", "BROAD", "BROWN", "BUYER", "CATCH",
    "CAUSE", "CHEAP", "CHECK", "CHESS", "CIVIL", "CLASS", "CLOSE", "COUGH", "COULD", "COVER", "CRASH", "CRAZY", "CREAM",
    "CRIME", "CROSS", "CROWD", "CYCLE", "DAILY", "DEATH", "DELAY", "DIRTY", "DOUBT", "DOZEN", "DRAMA", "DRIVE", "EARLY",
    "ENEMY", "ERROR", "EVENT", "EVERY", "EXACT", "EXIST", "FALSE", "FANCY", "FAULT", "FEWER", "FIFTH", "FIGHT", "FINAL",
    "FIRST", "FLOAT", "FORCE", "FORTH", "FORUM", "FOUND", "FRONT", "FUNNY", "GIVEN", "GOING", "GRAND", "GRANT", "GREAT",
    "GROUP", "GROWN", "HEAVY", "HELLO", "HUMAN", "HUMOR", "IRONY", "ISSUE", "JOINT", "JUDGE", "KNOWN", "LARGE", "LATER",
    "LEARN", "LEAST", "LEAVE", "LEGAL", "LIMIT", "LOGIC", "LOOSE", "LOWER", "MAJOR", "MAKER", "MATCH", "MAYBE", "MAYOR",
    "MEANT", "MEDIA", "MIGHT", "MINOR", "MINUS", "MIXED", "MORAL", "MOTOR", "MOUNT", "MOUTH", "NEVER", "NEWLY", "NURSE",
    "OCCUR", "OFFER", "OFTEN", "OTHER", "OUGHT", "OWNER", "PAUSE", "PHASE", "PHONE", "PIECE", "PITCH", "PLACE", "PLAIN",
    "PLAZA", "PRESS", "PRICE", "PRIDE", "PRIME", "PRINT", "PRIOR", "QUICK", "QUITE", "QUOTE", "RAISE", "RANGE", "RAPID",
    "RATIO", "READY", "REPLY", "RIGHT", "ROUGH", "ROUTE", "RURAL", "SCOPE", "SCORE", "SENSE", "SERVE", "SEVEN", "SHALL",
    "SHARP", "SHEET", "SHIFT", "SHOCK", "SHORT", "SHOWN", "SIGHT", "SINCE", "SKILL", "SMALL", "SMART", "SMOKE", "SOLID",
    "SOLVE", "SORRY", "SOUTH", "SPEAK", "SPEED", "SPEND", "SPLIT", "STAFF", "STAND", "START", "STATE", "STICK", "STILL",
    "STOCK", "STUDY", "STUFF", "STYLE", "SUPER", "TASTE", "THANK", "THEIR", "THERE", "THESE", "THICK", "THING", "THINK",
    "THIRD", "THOSE", "THREE", "THROW", "TIGHT", "TIMES", "TIRED", "TITLE", "TOPIC", "TOTAL", "TOUCH", "TOUGH", "TRACK",
    "TRADE", "TRIAL", "TRIED", "TRUCK", "TRULY", "TWICE", "UNDER", "UNTIL", "UPPER", "UPSET", "URBAN", "USUAL", "VALID",
    "VITAL", "WASTE", "WHERE", "WHICH", "WHILE", "WHITE", "WHOLE", "WHOSE", "WOMAN", "WORRY", "WORSE", "WORST", "WORTH",
    "WOULD", "WOUND", "WRITE", "WRONG", "YIELD", "YOUNG"};

constexpr int ANSWER_COUNT = sizeof(ANSWERS) / sizeof(ANSWERS[0]);

}  // namespace

void score(const std::string& guess, const std::string& answer, Mark out[WORD_LEN]) {
  int left[26] = {};
  for (int i = 0; i < WORD_LEN; ++i) {
    out[i] = Mark::Absent;
    if (guess[i] == answer[i]) {
      out[i] = Mark::Correct;
    } else if (answer[i] >= 'A' && answer[i] <= 'Z') {
      ++left[answer[i] - 'A'];
    }
  }
  for (int i = 0; i < WORD_LEN; ++i) {
    if (out[i] == Mark::Correct) continue;
    const char ch = guess[i];
    if (ch >= 'A' && ch <= 'Z' && left[ch - 'A'] > 0) {
      out[i] = Mark::Present;
      --left[ch - 'A'];
    }
  }
}

bool isWord(const std::string& upper) {
  if (upper.size() != WORD_LEN) return false;
  for (const char* w : ANSWERS) {
    if (upper == w) return true;
  }
  for (const char* w : EXTRA) {
    if (upper == w) return true;
  }
  return false;
}

std::string packWord(const int day) {
  // Every word once before any repeats: a fixed stride through the pack.
  const int i = ((day % ANSWER_COUNT) * 97 + 13) % ANSWER_COUNT;
  std::string w = ANSWERS[i < 0 ? i + ANSWER_COUNT : i];
  return w.size() == WORD_LEN ? w : "CRANE";
}

int packSize() { return ANSWER_COUNT; }

}  // namespace fos::puzzles
