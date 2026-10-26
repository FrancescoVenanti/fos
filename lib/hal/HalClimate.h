#pragma once

// FOs: room temperature and humidity from the on-board sensor (the Sticky's
// SHT40). The simulator has its own implementation with fixed readings.
class HalClimate {
 public:
  // False when the board has no sensor or the read failed.
  bool read(float& tempC, float& humidityPct);

 private:
  bool begun = false;
  bool present = false;
};

extern HalClimate halClimate;
