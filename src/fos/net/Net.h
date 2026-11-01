#pragma once

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

// HTTP request to any URL (Lichess, the terminal relay, Open-Meteo), with an
// optional bearer token. Follows redirects; a POST becomes a GET after
// 301/302/303, as browsers do.
Response request(const char* method, const std::string& url, const std::string& body, const std::string& bearer,
                 unsigned long timeoutMs = 8000);
// A public API's URL. In the simulator, FOS_SIM_DIRECT_BASE (tests) sends
// "https://host/path" to "<base>/direct/host/path" instead.
std::string directUrl(const std::string& url);
// Percent-encodes a query value.
std::string urlEncode(const std::string& value);

}  // namespace fos::net
