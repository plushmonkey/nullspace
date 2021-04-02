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

    u8* pkt = (u8*)buffer.data;
    size_t size = bytes_recv;

    if (encrypt.key1 != 0 || encrypt.key2 != 0) {
      encrypt.Decrypt(pkt, size);
      --size;  // Drop crc
    }

    u8 type = buffer.ReadU8();

    if (type == 0x00) {  // Core packet
      type = buffer.ReadU8();

      printf("Received core packet of type 0x%02X\n", type);

      switch (type) {
        case 0x02: { // Encryption response
          // Send password packet now
          u8 data[kMaxPacketSize];
          NetworkBuffer buffer(data, kMaxPacketSize);

          char name[32] = {};
          char password[32] = {};

          strcpy(name, "nullspace");
          strcpy(password, "none");

          buffer.WriteU8(0x24); // Continuum password packet
          buffer.WriteU8(0x00); // New user
          buffer.WriteString(name, 32);
          buffer.WriteString(password, 32);
          buffer.WriteU32(1178436307); // Machine ID
          buffer.WriteU8(0x00); // connect type
          buffer.WriteU16(240); // Time zone bias
          buffer.WriteU16(0); // Unknown
          buffer.WriteU16(40); // Version
          buffer.WriteU32(0xA5);
          buffer.WriteU32(0x00);
          buffer.WriteU32(0); // permission id

          buffer.WriteU32(0);
          buffer.WriteU32(0);
          buffer.WriteU32(0);

          for (int i = 0; i < 16; ++i) {
            buffer.WriteU32(0);
          }

          Send(buffer);
        } break;
        case 0x03: {
          printf("Got reliable message\n");
          packet_sequencer.OnReliableMessage(*this, pkt, size);
        } break;
        case 0x04: {
          printf("Got reliable ack\n");
          packet_sequencer.OnReliableAck(*this, pkt, size);
        } break;
        case 0x10: {  // Continuum encryption response
          u32 key1 = buffer.ReadU32();
          u32 key2 = buffer.ReadU32();

          printf("Received encryption response with keys %08X, %08X\n", key1, key2);

#pragma pack(push, 1)
          struct {
            u8 core;
            u8 type;
            u32 key;
            u16 flag;
          } response;
#pragma pack(pop)

          response.core = 0x00;
          response.type = 0x11;
          response.key = key1;
          response.flag = 0x0001;

          this->Send((u8*)&response, sizeof(response));

          if (!encrypt.ExpandKey(key1, key2)) {
            fprintf(stderr, "Failed to expand key.\n");
          } else {
            printf("Successfully expanded continuum encryption keys.\n");
          }
        } break;
        case 0x12: {  // Continuum key expansion request
          u32 table[20];
          u32 seed = buffer.ReadU32();

          printf("Sending key expansion response for key %08X\n", seed);
          if (encrypt.LoadTable(table, seed)) {
            u8 data[kMaxPacketSize];
            NetworkBuffer buffer(data, kMaxPacketSize);

            buffer.WriteU8(0x00);
            buffer.WriteU8(0x13);
            buffer.WriteU32(seed);

            for (size_t i = 0; i < 20; ++i) {
              buffer.WriteU32(table[i]);
            }

            Send(buffer);
          } else {
            fprintf(stderr, "Failed to load table for key expansion request.\n");
          }
        } break;
        default: {
        } break;
      }
    } else {
      printf("Received non-core packet of type 0x%02X\n", type);
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

size_t Connection::Send(NetworkBuffer& buffer) { return Send(buffer.data, buffer.GetSize()); }

size_t Connection::Send(u8* data, size_t size) {
  sockaddr_in addr;
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  if (encrypt.key1 || encrypt.key2) {
    encrypt.Encrypt(data, size);
    ++size; // Add crc
  }

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
