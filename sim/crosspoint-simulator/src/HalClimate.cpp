#include "HalClimate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool HalClimate::read(float &tempC, float &humidityPct) {
  const char *value = std::getenv("FOS_SIM_CLIMATE");
  if (value && std::strcmp(value, "none") == 0)
    return false;
  tempC = 21.4f;
  humidityPct = 47.0f;
  if (value)
    std::sscanf(value, "%f,%f", &tempC, &humidityPct);
  return true;
}

HalClimate halClimate;
