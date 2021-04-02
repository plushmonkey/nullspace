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

const char* kLoginResponses[] = {
  "Ok",
  "Unregistered player",
  "Bad password",
  "Arena is full",
  "Locked out of zone",
  "Permission only arena",
  "Permission to spectate only",
  "Too many points to play here",
  "Connection is too slow",
  "Permission only arena",
  "Server is full",
  "Invalid name",
  "Offensive name",
  "No active biller",
  "Server busy, try later",
  "Not enough usage to play here",
  "Restricted zone",
  "Demo version detected",
  "Too many demo users",
  "Demo versions not allowed",
  "Restricted zone, mod access required"
};

Connection::Connection(MemoryArena& perm_arena, MemoryArena& temp_arena)
    : remote_addr(), buffer(perm_arena, kMaxPacketSize), temp_arena(temp_arena) {}

Connection::TickResult Connection::Tick() {
  sockaddr_in addr = {};
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  buffer.Reset();
  packet_sequencer.Tick(*this);

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

    buffer.write += bytes_recv;

    u8* pkt = (u8*)buffer.data;
    size_t size = bytes_recv;

    if (encrypt.key1 != 0 || encrypt.key2 != 0) {
      encrypt.Decrypt(pkt, size);
      --size;  // Drop crc
    }

    ProcessPacket(pkt, size);
  }

  return TickResult::Success;
}

void Connection::ProcessPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size);
  buffer.write += size;

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
        packet_sequencer.OnReliableMessage(*this, pkt, size);
      } break;
      case 0x04: {
        packet_sequencer.OnReliableAck(*this, pkt, size);
      } break;
      case 0x0E: {
        // Packet cluster
        while (buffer.read < buffer.write) {
          u8 cluster_pkt_size = buffer.ReadU8();

          ProcessPacket(buffer.read, cluster_pkt_size);

          buffer.read += cluster_pkt_size;
        }
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

    switch (type) {
      case 0x01: { // PlayerID change
        u16 pid = buffer.ReadU16();
        printf("Player id: %d\n", pid);
      } break;
      case 0x02: { // In game
        printf("Now in game\n");
      } break;
      case 0x07: { // Chat
        u8 type = buffer.ReadU8();
        u8 sound = buffer.ReadU8();
        u16 sender_id = buffer.ReadU16();

        size_t len = (size_t)(buffer.write - buffer.read);
        char* mesg = buffer.ReadString(len);

        printf("%*s\n", (u32)len, mesg);
      } break;
      case 0x0A: { // Password packet response
        u8 response = buffer.ReadU8();

        printf("Login response: %s\n", kLoginResponses[response]);

        if (response == 0x00) {
          u8 data[kMaxPacketSize];
          NetworkBuffer write(data, kMaxPacketSize);

          char arena[16] = {};

          write.WriteU8(0x01); // type
          write.WriteU8(0x08); // ship number
          write.WriteU16(0x00); // allow audio
          write.WriteU16(1920); // x res
          write.WriteU16(1080); // y res
          write.WriteU16(0xFFFF); // Arena number
          write.WriteString(arena, 16);
          Send(write);
        }
      } break;
      default: {
      } break;
    }
  }
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
    u8* dest = temp_arena.Allocate(size + 1);
    encrypt.Encrypt(data, dest, size);
    ++size; // Add crc
    data = dest;
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
