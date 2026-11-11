#include "SdMount.h"

#include <HalStorage.h>

#if FREEINK_DEVICE_STICKY && !defined(SIMULATOR)

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <SdFat.h>
#include <driver/gpio.h>

#include <cstring>

namespace fos::sdcard {
namespace {

constexpr int ATTEMPTS = 3;
constexpr unsigned long POWER_OFF_MS = 300;

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

void powerOff() {
  const auto& sd = BoardConfig::ACTIVE.sd;
  if (sd.powerEnable < 0) return;
  gpio_hold_dis(static_cast<gpio_num_t>(sd.powerEnable));
  pinMode(sd.powerEnable, OUTPUT);
  digitalWrite(sd.powerEnable, sd.powerActiveHigh ? LOW : HIGH);
  // CS low too, so the card is not powered through its chip-select pull-up.
  pinMode(sd.cs, OUTPUT);
  digitalWrite(sd.cs, LOW);
  delay(POWER_OFF_MS);
}

// Why the card does not mount, from reads only.
void explainFailure() {
  const auto& cfg = BoardConfig::ACTIVE.sd;
  const uint32_t hz = cfg.spiHz != 0 ? cfg.spiHz : 40000000;
  SdSpiCard card;
  if (!card.begin(SdSpiConfig(cfg.cs, SHARED_SPI, hz))) {
    LOG_ERR("SD", "Card does not answer: error 0x%02X data 0x%02X", card.errorCode(), card.errorData());
    return;
  }
  static uint8_t sec[512];
  const uint32_t sectors = card.sectorCount();
  LOG_ERR("SD", "Card answers: type %d, %lu sectors (%lu MB)", card.type(), static_cast<unsigned long>(sectors),
          static_cast<unsigned long>(sectors / 2048));
  if (!card.readSector(0, sec)) {
    LOG_ERR("SD", "Sector 0 unreadable: error 0x%02X", card.errorCode());
    return;
  }
  const bool sig0 = sec[510] == 0x55 && sec[511] == 0xAA;
  const uint8_t type = sec[450];
  const uint32_t lba = le32(sec + 454);
  LOG_ERR("SD", "Sector 0: signature %s, first partition type 0x%02X at %lu, oem '%.8s'", sig0 ? "ok" : "MISSING", type,
          static_cast<unsigned long>(lba), reinterpret_cast<const char*>(sec + 3));
  // A card formatted without a partition table has the boot sector at 0.
  const bool superfloppy = sig0 && (memcmp(sec + 3, "EXFAT   ", 8) == 0 || memcmp(sec + 82, "FAT32   ", 8) == 0);
  const uint32_t bootLba = superfloppy ? 0 : lba;
  if (!sig0 || (!superfloppy && (lba == 0 || lba >= sectors))) return;
  if (bootLba != 0 && !card.readSector(bootLba, sec)) {
    LOG_ERR("SD", "Boot sector %lu unreadable: error 0x%02X", static_cast<unsigned long>(bootLba), card.errorCode());
    return;
  }
  const bool sig = sec[510] == 0x55 && sec[511] == 0xAA;
  const bool exfat = memcmp(sec + 3, "EXFAT   ", 8) == 0;
  const bool fat32 = memcmp(sec + 82, "FAT32   ", 8) == 0;
  LOG_ERR("SD", "Boot sector %lu: signature %s, %s, bytes/sector %u, sectors/cluster %u",
          static_cast<unsigned long>(bootLba), sig ? "ok" : "MISSING",
          exfat   ? "exFAT"
          : fat32 ? "FAT32"
                  : "UNKNOWN",
          exfat ? 1u << sec[108] : static_cast<unsigned>(sec[11] | sec[12] << 8),
          exfat ? 1u << sec[109] : static_cast<unsigned>(sec[13]));
  card.end();
}

}  // namespace

void powerCycleCard() {
  const auto& sd = BoardConfig::ACTIVE.sd;
  if (sd.powerEnable < 0) return;
  powerOff();
  digitalWrite(sd.powerEnable, sd.powerActiveHigh ? HIGH : LOW);
  delay(50);  // supply settle before SdFat clocks the card
}

bool mount() {
  for (int attempt = 1; attempt <= ATTEMPTS; ++attempt) {
    if (Storage.begin()) {
      if (attempt > 1) LOG_INF("SD", "Card mounted on attempt %d", attempt);
      return true;
    }
    LOG_ERR("SD", "Mount attempt %d failed", attempt);
    if (attempt < ATTEMPTS) powerOff();
  }
  explainFailure();
  return false;
}

}  // namespace fos::sdcard

// Called by the patched SdFat (patch 0005) when the card ignores even a reset:
// cut its supply, power it again and let SdFat initialize it.
extern "C" bool sdfat_power_cycle_hook() {
  static unsigned count = 0;
  const auto& sd = BoardConfig::ACTIVE.sd;
  if (sd.powerEnable < 0) return false;
  fos::sdcard::powerCycleCard();
  LOG_ERR("SD", "Card stopped answering; power-cycled it (%u since boot)", ++count);
  return true;
}

#else

namespace fos::sdcard {
void powerCycleCard() {}
bool mount() { return Storage.begin(); }
}  // namespace fos::sdcard

#endif
