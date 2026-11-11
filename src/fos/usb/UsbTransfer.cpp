#include "UsbTransfer.h"

#ifdef SIMULATOR

namespace fos::usb {
void begin() {}
bool poll() { return false; }
}  // namespace fos::usb

#else

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <esp_rom_sys.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>

#include "util/BookCacheUtils.h"

namespace fos::usb {
namespace {

constexpr unsigned long BASE_BAUD = 115200;
constexpr size_t CHUNK = 4096;
constexpr unsigned long DATA_TIMEOUT_MS = 5000;
constexpr unsigned long SESSION_IDLE_MS = 30000;
constexpr const char* PREFIX = "@fos ";

HardwareSerial& port = Serial0;
char line[600];
size_t lineLen = 0;
bool fastBaud = false;
unsigned long lastCommandAt = 0;
uint8_t chunk[CHUNK];

// Replies go through the ROM printer, the same path as the log, so a reply is
// never interleaved with a log line mid-way.
void reply(const char* fmt, ...) {
  char buf[400];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  esp_rom_printf("@fos %s\n", buf);
}

void setBaud(unsigned long baud) {
  port.flush();
  delay(20);  // let the last reply leave the FIFO
  port.updateBaudRate(baud);
}

void endSession() {
  if (fastBaud) setBaud(BASE_BAUD);
  fastBaud = false;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// "/books/x.epub" only: absolute, no "..", nothing hidden.
bool safePath(const char* path) {
  if (path[0] != '/' || strstr(path, "..") != nullptr || strstr(path, "/.") != nullptr) return false;
  return strlen(path) < 256;
}

void list(const char* dir, bool all) {
  HalFile d = Storage.open(dir);
  if (!d || !d.isDirectory()) {
    reply("err not a folder");
    return;
  }
  char name[256];
  while (HalFile f = d.openNextFile()) {
    f.getName(name, sizeof(name));
    if (all || name[0] != '.')
      reply("%c %u %s", f.isDirectory() ? 'd' : 'f', f.isDirectory() ? 0u : (unsigned)f.size(), name);
    f.close();
  }
  d.close();
  reply("end");
}

// Reads exactly `len` bytes or gives up after DATA_TIMEOUT_MS without data.
bool readExact(uint8_t* dst, size_t len) {
  size_t got = 0;
  unsigned long last = millis();
  while (got < len) {
    const int avail = port.available();
    if (avail > 0) {
      got += port.read(dst + got, std::min(len - got, static_cast<size_t>(avail)));
      last = millis();
    } else if (millis() - last > DATA_TIMEOUT_MS) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

// CRC-32 of a file as the card returns it, so a write the card garbled is caught.
bool cardCrc(const char* path, uint32_t& crc, unsigned long& size) {
  HalFile f;
  if (!Storage.openFileForRead("USB", path, f)) return false;
  crc = 0;
  size = 0;
  int n;
  while ((n = f.read(chunk, CHUNK)) > 0) {
    crc = crc32Update(crc, chunk, static_cast<size_t>(n));
    size += static_cast<unsigned long>(n);
  }
  f.close();
  return n == 0;
}

void put(char* args, bool replace) {
  char* end = nullptr;
  const unsigned long size = strtoul(args, &end, 10);
  if (end == args || *end != ' ') return reply("err bad put");
  char* crcText = end + 1;
  const uint32_t expected = strtoul(crcText, &end, 16);
  if (end == crcText || *end != ' ') return reply("err bad put");
  const char* path = end + 1;
  if (!safePath(path)) return reply("err bad path");
  if (!replace && Storage.exists(path)) return reply("err exists");

  const std::string part = std::string(path) + ".part";
  Storage.remove(part.c_str());
  HalFile out;
  if (!Storage.openFileForWrite("USB", part.c_str(), out)) return reply("err cannot create file");
  HalPowerManager::Lock fullSpeed;  // the transfer blocks the loop, which would idle the CPU down
  LOG_INF("USB", "Receiving %s (%lu bytes)", path, size);
  reply("ready");

  uint32_t crc = 0;
  unsigned long received = 0;
  bool ok = true;
  const char* error = "";
  while (received < size) {
    const size_t n = std::min(CHUNK, static_cast<size_t>(size - received));
    if (!readExact(chunk, n)) {
      ok = false;
      error = "timeout";
      break;
    }
    if (out.write(chunk, n) != n) {
      ok = false;
      error = "write failed, card full?";
      break;
    }
    crc = crc32Update(crc, chunk, n);
    received += n;
    reply("ack %lu", received);
  }
  const bool closed = out.close();
  if (ok && !closed) {
    ok = false;
    error = "write failed";
  }
  if (ok && crc != expected) {
    ok = false;
    error = "checksum mismatch";
  }
  uint32_t onCard = 0;
  unsigned long cardSize = 0;
  if (ok && (!cardCrc(part.c_str(), onCard, cardSize) || onCard != expected || cardSize != size)) {
    ok = false;
    error = "card read-back mismatch";
  }
  if (ok && !Storage.replaceFile(part.c_str(), path)) {
    ok = false;
    error = "cannot rename";
  }
  if (!ok) {
    Storage.remove(part.c_str());
    LOG_ERR("USB", "Transfer of %s failed: %s", path, error);
    // Drop whatever is still arriving so it is not read as commands.
    delay(200);
    while (port.available() > 0) port.read();
    return reply("err %s", error);
  }
  clearBookCache(path);
  if (library::isLibraryBookFile(path)) library::markLibraryIndexDirty();
  LOG_INF("USB", "Received %s", path);
  reply("done");
}

void handle(char* cmd) {
  lastCommandAt = millis();
  char* args = strchr(cmd, ' ');
  if (args) *args++ = '\0';
  const char* rest = args ? args : "";
  if (strcmp(cmd, "hello") == 0) {
    reply("ok FOs-usb 1");
  } else if (strcmp(cmd, "baud") == 0) {
    const unsigned long baud = strtoul(rest, nullptr, 10);
    if (baud < 9600 || baud > 3000000) return reply("err bad baud");
    reply("ok");
    setBaud(baud);
    fastBaud = baud != BASE_BAUD;
  } else if (strcmp(cmd, "ls") == 0) {
    const bool all = strncmp(rest, "-a", 2) == 0;
    const char* dir = all ? rest + 2 + (rest[2] == ' ') : rest;
    list(dir[0] ? dir : "/", all);
  } else if (strcmp(cmd, "mkdir") == 0) {
    if (!safePath(rest)) return reply("err bad path");
    reply(Storage.exists(rest) || Storage.mkdir(rest) ? "ok" : "err cannot create folder");
  } else if (strcmp(cmd, "rm") == 0) {
    if (!safePath(rest)) return reply("err bad path");
    HalFile f = Storage.open(rest);
    const bool isFile = f && !f.isDirectory();
    if (f) f.close();
    if (!isFile) return reply("err not a file");
    if (!Storage.remove(rest)) return reply("err cannot delete");
    clearBookCache(rest);
    if (library::isLibraryBookFile(rest)) library::markLibraryIndexDirty();
    reply("ok");
  } else if (strcmp(cmd, "crc") == 0) {
    if (!safePath(rest)) return reply("err bad path");
    HalPowerManager::Lock fullSpeed;
    uint32_t crc = 0;
    unsigned long size = 0;
    if (!cardCrc(rest, crc, size)) return reply("err cannot read");
    reply("ok %lu %08lx", size, static_cast<unsigned long>(crc));
  } else if (strcmp(cmd, "put") == 0 || strcmp(cmd, "put!") == 0) {
    put(args ? args : const_cast<char*>(""), cmd[3] == '!');
  } else if (strcmp(cmd, "end") == 0) {
    reply("ok");
    endSession();
  } else {
    reply("err unknown command");
  }
}

}  // namespace

void begin() {
  port.setRxBufferSize(CHUNK * 4);
  port.begin(BASE_BAUD);
}

bool poll() {
  if (fastBaud && millis() - lastCommandAt > SESSION_IDLE_MS) endSession();
  bool handled = false;
  while (port.available() > 0) {
    const int c = port.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineLen < sizeof(line) - 1) line[lineLen++] = static_cast<char>(c);
      continue;
    }
    line[lineLen] = '\0';
    lineLen = 0;
    if (strncmp(line, PREFIX, strlen(PREFIX)) != 0) continue;
    handle(line + strlen(PREFIX));
    lastCommandAt = millis();  // a transfer can outlast the idle timeout; count from its end
    handled = true;
  }
  return handled;
}

}  // namespace fos::usb

#endif
