#pragma once

// FOs: Windows MD5Builder for the simulator. Buffers the input and hashes it
// with CNG in calculate(); simulator inputs are small.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "HostWin.h"
#include "WString.h"

class MD5Builder {
public:
  MD5Builder() { memset(digest_, 0, sizeof(digest_)); }

  void begin() { data_.clear(); }
  void add(const uint8_t *data, size_t len) { data_.append(reinterpret_cast<const char *>(data), len); }
  void add(const char *str) {
    if (str) add(reinterpret_cast<const uint8_t *>(str), strlen(str));
  }
  void calculate() {
    host_win::hash(host_win::HashAlg::Md5, reinterpret_cast<const uint8_t *>(data_.data()), data_.size(), digest_);
  }

  String toString() const {
    char hex[33];
    for (int i = 0; i < 16; i++) {
      snprintf(hex + i * 2, 3, "%02x", digest_[i]);
    }
    return String(hex);
  }

private:
  std::string data_;
  uint8_t digest_[16];
};
