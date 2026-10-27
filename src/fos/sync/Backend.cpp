#include "Backend.h"

#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cctype>
#include <cstdlib>

#include "WifiCredentialStore.h"
#include "fos/settings/FosSettings.h"

namespace fos::net {
namespace {

constexpr unsigned long HEALTH_CACHE_MS = 5000;
unsigned long lastHealthAt = 0;
bool lastHealthOk = false;

bool waitConnected(const unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

std::string joinUrl(const std::string& base, const std::string& path) {
  if (!base.empty() && base.back() == '/') return base.substr(0, base.size() - 1) + path;
  return base + path;
}

}  // namespace

bool ensureWifi(const unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
#ifdef SIMULATOR
  // No radio on the desktop: the host network stands in for Wi-Fi, and
  // online/offline comes down to whether the backend answers.
  WiFi.begin("Simulator", "");
  return waitConnected(timeoutMs);
#else
  const size_t count = WIFI_STORE.getCredentialCount();
  for (size_t i = 0; i < count; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred) continue;
    LOG_DBG("NET", "Joining %s", cred->ssid.c_str());
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
    if (waitConnected(timeoutMs / (count > 0 ? count : 1) + 2000)) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      return true;
    }
    WiFi.disconnect();
  }
  return false;
#endif
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  // Forget the cached answer: the next check must reconnect and ask again.
  lastHealthAt = 0;
  lastHealthOk = false;
}

Response request(const char* method, const std::string& url, const std::string& body, const std::string& bearer,
                 const unsigned long timeoutMs) {
  Response out;
  freeink::SecureHttpClient http;
  // No CA bundle ships with the transport (as everywhere in CrossPoint); HTTPS
  // stays encrypted but the certificate is not verified.
  http.setInsecure();
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(5);
  http.setUserAgent("FOs");
  if (!http.begin(url)) return out;
  http.addHeader("Accept", "application/json");
  if (!bearer.empty()) http.addHeader("Authorization", "Bearer " + bearer);
  if (!body.empty()) http.addHeader("Content-Type", "application/json");
  out.status = http.sendRequest(method, body);
  if (out.status > 0) out.body = http.getString().c_str();
  http.end();
  if (out.status < 0) LOG_DBG("NET", "%s %s failed (%d)", method, url.c_str(), out.status);
  return out;
}

int stream(const std::string& url, const std::function<bool(const uint8_t*, size_t)>& onData,
           const unsigned long timeoutMs) {
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(5);
  http.setUserAgent("FOs");
  if (!http.begin(url)) return -1;
  const int status = http.GET(onData);
  http.end();
  if (status < 0) LOG_DBG("NET", "GET %s failed (%d)", url.c_str(), status);
  return status;
}

std::string directUrl(const std::string& url) {
#ifdef SIMULATOR
  const char* base = std::getenv("FOS_SIM_DIRECT_BASE");
  const size_t scheme = url.find("://");
  if (base && *base && scheme != std::string::npos) return std::string(base) + "/direct/" + url.substr(scheme + 3);
#endif
  return url;
}

std::string urlEncode(const std::string& value) {
  static constexpr char DIGITS[] = "0123456789ABCDEF";  // not HEX: an Arduino macro
  std::string out;
  for (const unsigned char ch : value) {
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += DIGITS[ch >> 4];
      out += DIGITS[ch & 15];
    }
  }
  return out;
}

Response backendGet(const std::string& path, const unsigned long timeoutMs) {
  const FosSettings& s = settings();
  if (s.backendUrl.empty()) return {};
  return request("GET", joinUrl(s.backendUrl, path), "", s.backendKey, timeoutMs);
}

Response backendPost(const std::string& path, const std::string& json, const unsigned long timeoutMs) {
  const FosSettings& s = settings();
  if (s.backendUrl.empty()) return {};
  return request("POST", joinUrl(s.backendUrl, path), json, s.backendKey, timeoutMs);
}

bool backendOnline() {
  if (lastHealthAt != 0 && millis() - lastHealthAt < HEALTH_CACHE_MS) return lastHealthOk;
  lastHealthOk = WiFi.status() == WL_CONNECTED && backendGet("/api/v1/health", 3000).ok();
  lastHealthAt = millis();
  return lastHealthOk;
}

}  // namespace fos::net
