#pragma once

// FOs: POSIX/newlib functions the firmware uses that MinGW-w64 lacks.
// Force-included into every simulator translation unit on Windows by
// scripts/fos_sim_env.py.

#ifdef _WIN32
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t strlcpy(char *dst, const char *src, size_t size) {
  const size_t len = strlen(src);
  if (size) {
    const size_t n = len >= size ? size - 1 : len;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return len;
}

static inline size_t strlcat(char *dst, const char *src, size_t size) {
  const size_t dlen = strnlen(dst, size);
  if (dlen == size) return size + strlen(src);
  return dlen + strlcpy(dst + dlen, src, size - dlen);
}

static inline int setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name)) return 0;
  return _putenv_s(name, value);
}

static inline int unsetenv(const char *name) { return _putenv_s(name, ""); }

// The simulator never sets the host clock; trusted-time sync is a no-op.
static inline int settimeofday(const struct timeval *tv, const void *tz) {
  (void)tv;
  (void)tz;
  return 0;
}

#ifdef __cplusplus
}
#endif
#endif
