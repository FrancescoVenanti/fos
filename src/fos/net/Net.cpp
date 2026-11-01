#include "Net.h"

#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cctype>
#include <cstdlib>

#include "WifiCredentialStore.h"

namespace fos::net {
namespace {

bool waitConnected(const unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace

bool ensureWifi(const unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
#ifdef SIMULATOR
  // No radio on the desktop: the host network stands in for Wi-Fi, and
  // online/offline comes down to whether the server answers.
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

}  // namespace fos::net
