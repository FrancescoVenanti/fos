#include "GameActivity.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdio>

#include "fos/core/Json.h"

namespace fos::chess {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char BOT_FILE[] = "/fos/chess/bot.json";
constexpr char LOCAL_FILE[] = "/fos/chess/local.json";

constexpr const char* COLORS[] = {"White", "Random", "Black"};
constexpr const char* LEVELS[] = {"1", "2", "3", "4", "5"};
constexpr const char* LEVEL_NAMES[] = {"Beginner: makes plenty of mistakes", "Casual: sees simple tactics",
                                       "Club: plays solid moves", "Strong: thinks a few moves ahead",
                                       "Strongest: thinks up to 4 seconds"};
constexpr const char* CLOCKS[] = {"Off", "10 min", "30 min", "60 min"};
constexpr int CLOCK_MINUTES[] = {0, 10, 30, 60};
constexpr const char* FLIPS[] = {"Turn board", "Keep board"};

constexpr int BAR_H = 40;
constexpr int VALUE[7] = {0, 1, 3, 3, 5, 9, 0};
constexpr unsigned long REPLY_DELAY_MS = 250;  // lets the human move reach the screen first

std::string clockText(const int64_t ms) {
  const int64_t s = (std::max<int64_t>(0, ms) + 999) / 1000;
  char out[16];
  snprintf(out, sizeof(out), "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
  return out;
}

}  // namespace

GameActivity::GameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode, OnlineGame online)
    : FosActivity("ChessGame", renderer, mappedInput), mode_(mode), online_(std::move(online)) {}

void GameActivity::onEnter() {
  switch (mode_) {
    case Mode::Bot:
      title_ = "Vs bot";
      setup_ = !load();
      break;
    case Mode::Local:
      title_ = "1v1";
      setup_ = !load();
      break;
    case Mode::Lichess:
    case Mode::Puzzle:
      title_ = mode_ == Mode::Lichess ? "Lichess" : "Daily puzzle";
      if (!online_.fen.empty()) {
        game_.resetFromFen(online_.fen);
      } else {
        game_.reset();
      }
      game_.playUciList(online_.moves);
      humanWhite_ = online_.white;
      break;
  }
  if (mode_ == Mode::Bot && !engine_) engine_ = std::make_unique<Engine>();
  // A saved bot game where the bot is to move resumes its thinking.
  if (mode_ == Mode::Bot && !setup_ && !game_.over() && !humanToMove()) {
    thinking_ = true;
    replyAt_ = millis() + REPLY_DELAY_MS;
  }
  clockAt_ = millis();
  updateTexts();
  FosActivity::onEnter();
}

void GameActivity::onExit() {
  // The local clock does not run while the screen is closed.
  if (mode_ == Mode::Local && !setup_) save();
  FosActivity::onExit();
}

bool GameActivity::load() {
  JsonDocument doc;
  if (!loadJson(mode_ == Mode::Bot ? BOT_FILE : LOCAL_FILE, doc)) return false;
  game_.reset();
  game_.playUciList(doc["moves"] | "");
  if (doc["over"] | false) {
    game_.setOver(static_cast<Result>(doc["result"] | static_cast<int>(Result::Resigned)), doc["whiteWon"] | false);
  }
  if (mode_ == Mode::Bot) {
    humanWhite_ = doc["white"] | true;
    level_ = doc["level"] | 2;
  } else {
    clockChoice_ = doc["clock"] | 0;
    autoFlip_ = doc["flip"] | true;
    clockMs_[0] = doc["whiteMs"] | 0LL;
    clockMs_[1] = doc["blackMs"] | 0LL;
  }
  return true;
}

void GameActivity::save() const {
  if (mode_ != Mode::Bot && mode_ != Mode::Local) return;
  JsonDocument doc;
  doc["moves"] = game_.uciMoves();
  const Result r = game_.result();
  if (r == Result::Resigned || r == Result::Timeout) {
    doc["over"] = true;
    doc["result"] = static_cast<int>(r);
    doc["whiteWon"] = game_.whiteWon();
  }
  if (mode_ == Mode::Bot) {
    doc["white"] = humanWhite_;
    doc["level"] = level_;
  } else {
    doc["clock"] = clockChoice_;
    doc["flip"] = autoFlip_;
    doc["whiteMs"] = clockMs_[0];
    doc["blackMs"] = clockMs_[1];
  }
  if (!saveJson(mode_ == Mode::Bot ? BOT_FILE : LOCAL_FILE, doc)) LOG_ERR("CHESS", "Cannot save the game");
}

void GameActivity::startGame() {
  game_.reset();
  setup_ = false;
  selected_ = -1;
  targets_ = 0;
  message_.clear();
  viewFlipped_ = false;
  if (mode_ == Mode::Bot) {
    humanWhite_ = colorChoice_ == 0 || (colorChoice_ == 1 && (esp_random() & 1));
    if (!humanWhite_) {
      thinking_ = true;
      replyAt_ = millis() + REPLY_DELAY_MS;
    }
  } else {
    clockMs_[0] = clockMs_[1] = CLOCK_MINUTES[clockChoice_] * 60000LL;
    clockAt_ = millis();
  }
  save();
  updateTexts();
  refresh(true);
}

bool GameActivity::humanToMove() const {
  if (game_.over() || thinking_ || sending_ || choosingPromotion_) return false;
  switch (mode_) {
    case Mode::Bot:
      return game_.position().whiteToMove == humanWhite_;
    case Mode::Local:
      return true;
    case Mode::Lichess:
      return online_.myTurn && game_.position().whiteToMove == humanWhite_;
    case Mode::Puzzle:
      return !puzzleDone_ && game_.position().whiteToMove == humanWhite_;
  }
  return false;
}

bool GameActivity::boardFlipped() const {
  bool flipped = !humanWhite_;
  if (mode_ == Mode::Local) flipped = autoFlip_ && !game_.position().whiteToMove;
  return flipped != viewFlipped_;
}

void GameActivity::select(const int square) {
  selected_ = -1;
  targets_ = 0;
  const int8_t v = game_.position().sq[square];
  if (!v || isWhite(v) != game_.position().whiteToMove) return;
  for (const Move& m : game_.position().legalMoves()) {
    if (m.from == square) targets_ |= 1ULL << m.to;
  }
  if (targets_) selected_ = square;
}

void GameActivity::playHuman(const Move& m) {
  const Position before = game_.position();
  const std::string uci = before.uci(m);
  if (!game_.play(m)) return;
  selected_ = -1;
  targets_ = 0;
  message_.clear();

  if (mode_ == Mode::Puzzle) {
    const bool expected = solved_ < online_.solution.size() && online_.solution[solved_] == uci;
    // Any mate counts, as on Lichess.
    if (!expected && game_.result() != Result::Checkmate) {
      game_.undo();
      message_ = "Not the move. Try again.";
      updateTexts();
      return;
    }
    ++solved_;
    if (solved_ >= online_.solution.size() || game_.over()) {
      puzzleDone_ = true;
      message_ = "Solved!";
    } else {
      message_ = "Correct. Keep going.";
      thinking_ = true;
      replyAt_ = millis() + 900;
    }
    updateTexts();
    return;
  }
  if (mode_ == Mode::Lichess) {
    sending_ = true;
    replyAt_ = millis() + REPLY_DELAY_MS;
    updateTexts();
    return;
  }
  afterMove();
}

void GameActivity::afterMove() {
  if (mode_ == Mode::Bot && !game_.over() && !humanToMove()) {
    thinking_ = true;
    replyAt_ = millis() + REPLY_DELAY_MS;
  }
  if (mode_ == Mode::Local) clockAt_ = millis();
  save();
  updateTexts();
}

void GameActivity::tickClock() {
  if (mode_ != Mode::Local || setup_ || CLOCK_MINUTES[clockChoice_] == 0) return;
  const unsigned long now = millis();
  const unsigned long elapsed = now - clockAt_;
  clockAt_ = now;
  // The clock starts with White's first move.
  if (game_.over() || game_.moves().empty()) return;
  int64_t& left = clockMs_[game_.position().whiteToMove ? 0 : 1];
  const int64_t before = left;
  left -= static_cast<int64_t>(elapsed);
  if (left <= 0) {
    left = 0;
    game_.setOver(Result::Timeout, !game_.position().whiteToMove);
    save();
    updateTexts();
    refresh(true);
    return;
  }
  if (before / 1000 != left / 1000) {
    updateTexts();
    refresh();
  }
}

void GameActivity::onTick() {
  tickClock();
  if ((thinking_ || sending_) && millis() >= replyAt_) {
    if (sending_) {
      sending_ = false;
      std::string error;
      Game previous = game_;
      previous.undo();
      const std::string uci = previous.position().uci(game_.moves().back());
      if (lichess::sendMove(online_.id, uci, error)) {
        online_.myTurn = false;
        message_ = "Move sent.";
      } else {
        game_.undo();
        message_ = error.empty() ? "Could not send the move." : error;
      }
    } else if (mode_ == Mode::Puzzle) {
      thinking_ = false;
      if (solved_ < online_.solution.size() && game_.playUci(online_.solution[solved_])) ++solved_;
      if (solved_ >= online_.solution.size()) {
        puzzleDone_ = true;
        message_ = "Solved!";
      }
    } else if (engine_) {
      const Move m = engine_->think(game_.position(), level_);
      thinking_ = false;
      LOG_DBG("CHESS", "Bot level %d: depth %d, %u nodes", level_, engine_->lastDepth(),
              static_cast<unsigned>(engine_->lastNodes()));
      if (m.valid()) game_.play(m);
      afterMove();
    }
    updateTexts();
    refresh();
  }
}

void GameActivity::command(const Command cmd) {
  switch (cmd) {
    case Command::Undo:
      if (mode_ == Mode::Bot) {
        // Back to the human's last turn.
        game_.undo();
        if (!humanToMove() && !game_.moves().empty()) game_.undo();
        if (game_.position().whiteToMove != humanWhite_ && game_.moves().empty()) {
          thinking_ = true;
          replyAt_ = millis() + REPLY_DELAY_MS;
        }
      } else {
        game_.undo();
      }
      selected_ = -1;
      targets_ = 0;
      save();
      break;
    case Command::Flip:
      viewFlipped_ = !viewFlipped_;
      break;
    case Command::New:
      if (!game_.over() && !game_.moves().empty()) {
        confirm_ = Command::New;
        return;
      }
      setup_ = true;
      break;
    case Command::Resign:
      if (!game_.over()) {
        confirm_ = Command::Resign;
        return;
      }
      break;
    case Command::Retry:
      game_.resetFromFen(online_.fen.empty() ? game_.start().fen() : online_.fen);
      game_.playUciList(online_.moves);
      solved_ = 0;
      puzzleDone_ = false;
      message_.clear();
      break;
    case Command::Hint:
      if (solved_ < online_.solution.size()) {
        Move m;
        if (game_.position().parseUci(online_.solution[solved_], m)) {
          select(m.from);
          message_ = "Move this piece.";
        }
      }
      break;
    case Command::Refresh:
      // Lichess: back to the list, which reloads.
      finish();
      return;
    case Command::None:
      break;
  }
  updateTexts();
  refresh(true);
}

void GameActivity::updateTexts() {
  const Position& pos = game_.position();
  // Material balance shown on the side ahead.
  int material[2] = {0, 0};
  for (const int8_t v : pos.sq) material[isWhite(v) ? 0 : 1] += VALUE[pieceType(v)];
  const int lead = material[0] - material[1];

  for (int side = 0; side < 2; ++side) {
    const bool white = side == 0;
    std::string& name = barText_[side][0];
    std::string& info = barText_[side][1];
    const bool human = white == humanWhite_;
    switch (mode_) {
      case Mode::Bot:
        name = human ? "You" : "Bot  \xC2\xB7  level " + std::to_string(level_);
        break;
      case Mode::Local:
        name = white ? "White" : "Black";
        break;
      case Mode::Lichess:
        name = human ? "You" : online_.opponent;
        break;
      case Mode::Puzzle:
        name = human ? "You" : "Puzzle  \xC2\xB7  " + std::to_string(online_.rating);
        break;
    }
    info.clear();
    if (mode_ == Mode::Local && CLOCK_MINUTES[clockChoice_] > 0) {
      info = clockText(clockMs_[side]);
    } else if ((white && lead > 0) || (!white && lead < 0)) {
      info = "+" + std::to_string(white ? lead : -lead);
    }
  }

  const Result r = game_.result();
  if (r != Result::Ongoing) {
    status_ = resultText(r);
    if (r == Result::Checkmate || r == Result::Resigned || r == Result::Timeout) {
      const bool whiteWon = game_.whiteWon();
      status_ += whiteWon ? "  \xC2\xB7  White wins" : "  \xC2\xB7  Black wins";
      if (mode_ == Mode::Bot)
        status_ = std::string(resultText(r)) + (whiteWon == humanWhite_ ? ". You win!" : ". The bot wins.");
    }
  } else if (!message_.empty()) {
    status_ = message_;
  } else if (thinking_ && mode_ == Mode::Bot) {
    status_ = "The bot is thinking...";
  } else if (sending_) {
    status_ = "Sending your move...";
  } else if (mode_ == Mode::Lichess && !online_.myTurn) {
    status_ = "Waiting for " + online_.opponent;
  } else if (mode_ == Mode::Puzzle && solved_ == 0) {
    status_ = std::string("Find the best move for ") + (humanWhite_ ? "White" : "Black");
  } else if (pos.inCheck()) {
    status_ = "Check!  " + game_.recentSan(28);
  } else {
    status_ = game_.recentSan(40);
    if (status_.empty()) status_ = pos.whiteToMove ? "White to move" : "Black to move";
  }

  buttonCount_ = 0;
  const auto add = [this](const Command cmd, const char* label) {
    buttons_[buttonCount_] = cmd;
    buttonLabels_[buttonCount_++] = label;
  };
  switch (mode_) {
    case Mode::Bot:
      add(Command::Undo, "Undo");
      add(Command::Flip, "Flip");
      add(Command::New, game_.over() ? "New game" : "New");
      break;
    case Mode::Local:
      add(Command::Undo, "Undo");
      add(Command::Resign, "Resign");
      add(Command::New, game_.over() ? "New game" : "New");
      break;
    case Mode::Lichess:
      add(Command::Flip, "Flip");
      add(Command::Refresh, "Refresh");
      break;
    case Mode::Puzzle:
      if (!puzzleDone_) add(Command::Hint, "Hint");
      add(Command::Retry, "Retry");
      add(Command::Flip, "Flip");
      break;
  }
}

void GameActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), setup_ ? "New game" : title_.c_str(), BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + 6);

  if (setup_) {
    drawSetup(c, area);
    return;
  }

  const bool flipped = boardFlipped();
  const Rect inner = area.inset(GUTTER, 0);
  drawBar(c, {inner.x, area.y, inner.w, BAR_H}, flipped);  // top: the side not at the bottom
  area.takeTop(BAR_H + 6 + BORDER);

  const Rect boardRect{(c.width() - BOARD) / 2, area.y, BOARD, BOARD};
  BoardStyle style;
  style.flipped = flipped;
  style.selected = selected_;
  style.targets = targets_;
  if (!game_.moves().empty()) {
    style.lastFrom = game_.moves().back().from;
    style.lastTo = game_.moves().back().to;
  }
  if (game_.position().inCheck()) style.checkSquare = game_.position().kingSquare(game_.position().whiteToMove);
  drawBoard(c, boardRect, game_.position(), style, SQUARE);
  area.takeTop(BOARD + BORDER + COORD_H + 2);

  drawBar(c, {inner.x, area.y, inner.w, BAR_H}, !flipped);
  area.takeTop(BAR_H + 4);

  const Rect buttons = body.inset(GUTTER, 0).takeBottom(MIN_TARGET + SHADOW + GUTTER);
  c.textIn({inner.x, area.y, inner.w, buttons.y - area.y}, font::BODY, status_.c_str(),
           game_.over() ? Tone::Black : Tone::Dark, Align::Center, game_.over());

  const int bw = (buttons.w - GAP * (buttonCount_ - 1)) / std::max(1, buttonCount_);
  for (int i = 0; i < buttonCount_; ++i) {
    const bool primary = buttons_[i] == Command::New && game_.over();
    button(c, {buttons.x + i * (bw + GAP), buttons.y, bw, MIN_TARGET + SHADOW}, buttonLabels_[i], BUTTON,
           primary ? ButtonStyle::Primary : ButtonStyle::Secondary, static_cast<int16_t>(i));
  }

  if (choosingPromotion_) drawPromotion(c);
  if (confirm_ == Command::New) {
    dialog(c, "New game?", "The game in progress will be lost.", "New game", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  } else if (confirm_ == Command::Resign) {
    dialog(c, "Resign?",
           game_.position().whiteToMove ? "White resigns and Black wins." : "Black resigns and White wins.", "Resign",
           DIALOG_OK, "Cancel", DIALOG_CANCEL);
  }
}

void GameActivity::drawBar(Canvas& c, const Rect& r, const bool whiteSide) const {
  const int side = whiteSide ? 0 : 1;
  const bool toMove = !game_.over() && game_.position().whiteToMove == whiteSide;
  // Whose move: inverted.
  c.fill(r, toMove ? Tone::Black : Tone::White);
  c.frame(r, BORDER, Tone::Black);
  const Tone ink = toMove ? Tone::White : Tone::Black;
  Rect inner = r.inset(12, 0);
  const int dot = 16;
  const Rect mark{inner.x, r.y + (r.h - dot) / 2, dot, dot};
  c.fill(mark, whiteSide ? Tone::White : Tone::Black);
  c.frame(mark, 2, toMove ? Tone::White : Tone::Black);
  inner.takeLeft(dot + 10);
  const std::string& info = barText_[side][1];
  if (!info.empty()) {
    const int w = c.textWidth(font::TITLE, info.c_str(), true);
    c.textIn(inner.takeRight(w), font::TITLE, info.c_str(), ink, Align::Right, true);
  }
  c.textIn(inner, font::BODY, barText_[side][0].c_str(), ink, Align::Left, true);
}

void GameActivity::drawPromotion(Canvas& c) const {
  c.hit({0, 0, c.width(), c.height()}, CANCEL_PROMOTION);
  const int size = 88;
  const int w = 4 * size + 3 * GAP + 2 * GUTTER;
  const int h = size + 2 * GUTTER + c.lineHeight(font::TITLE) + GAP;
  const Rect card{(c.width() - w) / 2, (c.height() - h) / 2, w, h};
  c.fill(card.offset(SHADOW * 2, SHADOW * 2), Tone::Black);
  c.fill(card, Tone::White);
  c.frame(card, BORDER, Tone::Black);
  Rect inner = card.inset(GUTTER);
  c.textIn(inner.takeTop(c.lineHeight(font::TITLE)), font::TITLE, "Promote to", Tone::Black, Align::Left, true);
  inner.takeTop(GAP);
  const bool white = game_.position().whiteToMove;
  constexpr int TYPES[] = {QUEEN, ROOK, BISHOP, KNIGHT};
  for (int i = 0; i < 4; ++i) {
    const Rect b{inner.x + i * (size + GAP), inner.y, size, size};
    const Rect face = block(c, b, Tone::Light, PROMOTE, static_cast<int16_t>(TYPES[i]));
    drawPiece(c, face.x + (face.w - 48) / 2, face.y + (face.h - 48) / 2, TYPES[i], white);
  }
}

void GameActivity::drawSetup(Canvas& c, Rect area) {
  area = area.inset(GUTTER, 0);
  area.takeTop(GAP);
  if (mode_ == Mode::Bot) {
    sectionLabel(c, area.takeTop(30).inset(-GUTTER, 0), "PLAY AS");
    segmented(c, area.takeTop(TAB_H), COLORS, 3, colorChoice_, SETUP_COLOR);
    area.takeTop(GAP * 2);
    sectionLabel(c, area.takeTop(30).inset(-GUTTER, 0), "BOT STRENGTH");
    segmented(c, area.takeTop(TAB_H), LEVELS, 5, level_ - 1, SETUP_LEVEL);
    area.takeTop(GAP);
    c.textIn(area.takeTop(c.lineHeight(font::BODY)), font::BODY, LEVEL_NAMES[level_ - 1], Tone::Dark);
    area.takeTop(GAP * 2);
    c.textWrapped(area.takeTop(90), font::BODY,
                  "The bot runs on this device, with no network. Your game is saved after every move.", Tone::Dark, 3);
  } else {
    sectionLabel(c, area.takeTop(30).inset(-GUTTER, 0), "CLOCK PER PLAYER");
    segmented(c, area.takeTop(TAB_H), CLOCKS, 4, clockChoice_, SETUP_CLOCK);
    area.takeTop(GAP * 2);
    sectionLabel(c, area.takeTop(30).inset(-GUTTER, 0), "AFTER EACH MOVE");
    segmented(c, area.takeTop(TAB_H), FLIPS, 2, autoFlip_ ? 0 : 1, SETUP_FLIP);
    area.takeTop(GAP * 2);
    c.textWrapped(area.takeTop(90), font::BODY,
                  "Pass the device between moves. With \"Turn board\" each player sees their pieces at the bottom.",
                  Tone::Dark, 3);
  }
  const Rect start{area.x, area.bottom() - GUTTER - ACTION_H, area.w, ACTION_H};
  button(c, start, "Start", START, ButtonStyle::Primary, 0, font::TITLE);
}

void GameActivity::onAction(const uint16_t action, const int16_t value) {
  if (confirm_ != Command::None) {
    const Command cmd = confirm_;
    confirm_ = Command::None;
    if (action == DIALOG_OK) {
      if (cmd == Command::New) {
        setup_ = true;
      } else if (cmd == Command::Resign) {
        game_.setOver(Result::Resigned, !game_.position().whiteToMove);
        save();
      }
      updateTexts();
    }
    refresh(true);
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case SQUARE: {
      if (!humanToMove()) return;
      if (selected_ >= 0 && (targets_ & (1ULL << value))) {
        std::vector<Move> options;
        for (const Move& m : game_.position().legalMoves()) {
          if (m.from == selected_ && m.to == value) options.push_back(m);
        }
        if (options.size() > 1) {
          promotion_ = options.front();
          choosingPromotion_ = true;
          return;
        }
        if (!options.empty()) playHuman(options.front());
        return;
      }
      select(value);
      return;
    }
    case PROMOTE:
      choosingPromotion_ = false;
      promotion_.promo = static_cast<uint8_t>(value);
      playHuman(promotion_);
      return;
    case CANCEL_PROMOTION:
      choosingPromotion_ = false;
      return;
    case BUTTON:
      if (value >= 0 && value < buttonCount_) command(buttons_[value]);
      return;
    case SETUP_COLOR:
      colorChoice_ = value;
      return;
    case SETUP_LEVEL:
      level_ = value + 1;
      return;
    case SETUP_CLOCK:
      clockChoice_ = value;
      return;
    case SETUP_FLIP:
      autoFlip_ = value == 0;
      return;
    case START:
      startGame();
      return;
    default:
      return;
  }
}

void GameActivity::onBack() {
  if (choosingPromotion_ || confirm_ != Command::None) {
    choosingPromotion_ = false;
    confirm_ = Command::None;
    requestUpdate();
    return;
  }
  // Leaving setup with a game in progress goes back to that game.
  if (setup_ && !game_.moves().empty() && !game_.over() && (mode_ == Mode::Bot || mode_ == Mode::Local)) {
    setup_ = false;
    requestUpdate();
    return;
  }
  finish();
}

}  // namespace fos::chess
