#pragma once

// FOs: Windows host services for the simulator. Kept free of <windows.h> so
// firmware headers never see its macros (IGNORE, ERROR, ABSOLUTE, ...);
// HostWin.cpp holds the Win32 calls.

#ifdef _WIN32
#include <cstddef>
#include <cstdint>
#include <string>

namespace host_win {

enum class HashAlg { Md5, Sha1, Sha256 };

// One-shot CNG hash; out must hold 16/20/32 bytes for MD5/SHA-1/SHA-256.
void hash(HashAlg alg, const uint8_t *data, size_t len, uint8_t *out);

// Creates an empty, uniquely named file in the user's temp folder.
std::string tempFilePath();

// rename() that replaces an existing destination, as POSIX rename() does.
bool renameReplace(const char *from, const char *to);

}  // namespace host_win
#endif
