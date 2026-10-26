#pragma once

// FOs: BSD-socket portability for the simulator's host network shims.
// Include only from .cpp files: on Windows it pulls in <winsock2.h>.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

namespace host_socket {
// Winsock needs WSAStartup before the first socket call.
inline bool init() {
  static const bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}
inline int closeSocket(int fd) { return ::closesocket(static_cast<SOCKET>(fd)); }
inline long sendBytes(int fd, const void *buf, size_t len, int flags) {
  return ::send(static_cast<SOCKET>(fd), static_cast<const char *>(buf), static_cast<int>(len), flags);
}
inline long recvBytes(int fd, void *buf, size_t len, int flags) {
  return ::recv(static_cast<SOCKET>(fd), static_cast<char *>(buf), static_cast<int>(len), flags);
}
inline void setTimeouts(int fd, int seconds) {
  const DWORD ms = static_cast<DWORD>(seconds) * 1000;
  setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms));
  setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms));
}
inline void setReuseAddr(int fd) {
  const int yes = 1;
  setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
}
}  // namespace host_socket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace host_socket {
inline bool init() { return true; }
inline int closeSocket(int fd) { return ::close(fd); }
inline long sendBytes(int fd, const void *buf, size_t len, int flags) { return ::send(fd, buf, len, flags); }
inline long recvBytes(int fd, void *buf, size_t len, int flags) { return ::recv(fd, buf, len, flags); }
inline void setTimeouts(int fd, int seconds) {
  timeval timeout{};
  timeout.tv_sec = seconds;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
inline void setReuseAddr(int fd) {
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}
}  // namespace host_socket
#endif
