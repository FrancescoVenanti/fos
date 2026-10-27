#pragma once

namespace fos::sync {

// Registers the fetchers that work without the backend:
// - Lock screen weather: Open-Meteo for Settings > Lock screen > Weather place
//   (the place wins over the backend when set; otherwise the backend).
// - Calculator rates: the backend, or Frankfurter (ECB rates) without it.
// - Calendar: the iCal link from Settings > Accounts when set (parsed on the
//   device), otherwise the backend.
// Each writes the same cache file as the backend download.
void registerDirectSources();

}  // namespace fos::sync
