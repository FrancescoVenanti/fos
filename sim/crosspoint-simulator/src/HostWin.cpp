// FOs: Win32 implementations behind HostWin.h.
#ifdef _WIN32
#include "HostWin.h"

#include <windows.h>
#include <bcrypt.h>

namespace host_win {

void hash(HashAlg alg, const uint8_t *data, size_t len, uint8_t *out) {
  LPCWSTR id = alg == HashAlg::Md5 ? BCRYPT_MD5_ALGORITHM : alg == HashAlg::Sha1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA256_ALGORITHM;
  const ULONG outLen = alg == HashAlg::Md5 ? 16 : alg == HashAlg::Sha1 ? 20 : 32;
  BCRYPT_ALG_HANDLE handle = nullptr;
  if (BCryptOpenAlgorithmProvider(&handle, id, nullptr, 0) != 0) return;
  BCryptHash(handle, nullptr, 0, const_cast<PUCHAR>(data), static_cast<ULONG>(len), out, outLen);
  BCryptCloseAlgorithmProvider(handle, 0);
}

std::string tempFilePath() {
  char dir[MAX_PATH];
  char path[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, dir) || !GetTempFileNameA(dir, "fos", 0, path)) return {};
  return path;
}

bool renameReplace(const char *from, const char *to) {
  return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

}  // namespace host_win
#endif
