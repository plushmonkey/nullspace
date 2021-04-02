#include "Connection.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>
#endif

namespace null {

Connection::Connection(MemoryArena& perm_arena, MemoryArena& temp_arena)
    : remote_addr(), buffer(perm_arena, kMaxPacketSize) {}

Connection::TickResult Connection::Tick() {
  sockaddr_in addr = {};
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  buffer.Reset();

  socklen_t socklen = sizeof(addr);
  int bytes_recv = recvfrom(fd, (char*)buffer.read, kMaxPacketSize, 0, (sockaddr*)&addr, &socklen);

  if (bytes_recv == 0) {
    this->connected = false;
    return TickResult::ConnectionClosed;
  } else if (bytes_recv < 0) {
    int err = WSAGetLastError();

    if (err == WSAEWOULDBLOCK) {
      return TickResult::Success;
    }

    fprintf(stderr, "Unexpected socket error: %d\n", err);
    this->Disconnect();
    return TickResult::ConnectionError;
  } else if (bytes_recv > 0) {
    assert(bytes_recv <= kMaxPacketSize);

    printf("Received data\n");

    u8 type = buffer.ReadU8();

    if (type == 0x00) {  // Core packet
      type = buffer.ReadU8();

      printf("Received core packet of type %d\n", type);
    } else {
      printf("Received non-core packet of type %d\n", type);
    }
  }

  return TickResult::Success;
}

ConnectResult Connection::Connect(const char* ip, u16 port) {
  inet_pton(AF_INET, ip, &this->remote_addr.addr);

  this->remote_addr.port = htons(port);
  this->remote_addr.family = AF_INET;

  this->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (this->fd < 0) {
    return ConnectResult::ErrorSocket;
  }

  this->connected = true;
  this->SetBlocking(false);

  return ConnectResult::Success;
}

size_t Connection::Send(const NetworkBuffer& buffer) { return Send(buffer.data, buffer.GetSize()); }

size_t Connection::Send(const u8* data, size_t size) {
  sockaddr_in addr;
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  int bytes = sendto(this->fd, (const char*)data, (int)size, 0, (sockaddr*)&addr, sizeof(addr));

  if (bytes <= 0) {
    Disconnect();
    return 0;
  }

  return bytes;
}

void Connection::Disconnect() {
  closesocket(this->fd);
  this->connected = false;
}

void Connection::SetBlocking(bool blocking) {
  unsigned long mode = blocking ? 0 : 1;

#ifdef _WIN32
  ioctlsocket(this->fd, FIONBIO, &mode);
#else
  int flags = fcntl(this->fd, F_GETFL, 0);

  flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

  fcntl(this->fd, F_SETFL, flags);
#endif
}

#ifdef _WIN32
struct NetworkInitializer {
  NetworkInitializer() {
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      fprintf(stderr, "Error WSAStartup: %d\n", WSAGetLastError());
      exit(1);
    }
  }
};

NetworkInitializer _net_init;
#endif

}  // namespace null
