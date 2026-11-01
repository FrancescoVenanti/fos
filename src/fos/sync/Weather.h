#pragma once

namespace fos::sync {

// Registers the lock screen weather fetcher: Open-Meteo for the place set in
// Settings > Lock screen > Weather place (looked up once, coordinates kept in
// /fos/lockscreen/place.json). Writes /fos/lockscreen/cache-weather.json.
void registerWeather();

}  // namespace fos::sync
