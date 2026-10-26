#include "HalClimate.h"

#include <EnvironmentSensor.h>

namespace {
freeink::EnvironmentSensor sensor;
}  // namespace

bool HalClimate::read(float& tempC, float& humidityPct) {
  if (!begun) {
    begun = true;
    present = sensor.begin();
  }
  return present && sensor.read(tempC, humidityPct);
}

HalClimate halClimate;
