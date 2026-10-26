#pragma once

// FOs: room climate. The simulator reports fixed values; set
// FOS_SIM_CLIMATE="<celsius>,<humidity>" to change them or "none" for a board
// without a sensor.
class HalClimate {
public:
  bool read(float &tempC, float &humidityPct);
};

extern HalClimate halClimate;
