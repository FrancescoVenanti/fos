#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace fos::net {

// Joins the first reachable saved network (Settings > Wi-Fi order). In the
// simulator the host network stands in for Wi-Fi. True when connected.
bool ensureWifi(unsigned long timeoutMs = 8000);
void wifiOff();

struct Response {
  int status = -1;  // HTTP status, < 0 on a transport failure
  std::string body;
  bool ok() const { return status >= 200 && status < 300; }
};

// Requests against Settings > Accounts > Backend address, with the backend key
// as a bearer token. path starts with "/api/v1/...".
Response backendGet(const std::string& path, unsigned long timeoutMs = 8000);
Response backendPost(const std::string& path, const std::string& json, unsigned long timeoutMs = 8000);
// Any URL (Lichess, Google, public APIs). Follows redirects (Apps Script
// answers with one); a POST becomes a GET after 301/302/303, as browsers do.
Response request(const char* method, const std::string& url, const std::string& body, const std::string& bearer,
                 unsigned long timeoutMs = 8000);
// GET that hands the body to onData in chunks instead of keeping it (large
// calendars). onData returns false to stop. Returns the HTTP status.
int stream(const std::string& url, const std::function<bool(const uint8_t*, size_t)>& onData,
           unsigned long timeoutMs = 15000);

// A public API's URL. In the simulator, FOS_SIM_DIRECT_BASE (tests) sends
// "https://host/path" to "<base>/direct/host/path" instead.
std::string directUrl(const std::string& url);
// Percent-encodes a query value.
std::string urlEncode(const std::string& value);

// Backend reachable right now (GET /api/v1/health, cached for a few seconds).
bool backendOnline();

}  // namespace fos::net
