#ifndef NULLSPACE_CONNECTION_H_
#define NULLSPACE_CONNECTION_H_

#include "../ArenaSettings.h"
#include "../Buffer.h"
#include "../MapHandler.h"
#include "../Types.h"
#include "Crypt.h"
#include "PacketSequencer.h"

namespace null {
enum class ConnectResult { Success, ErrorSocket, ErrorAddrInfo, ErrorConnect };

struct RemoteAddress {
  long addr;
  u16 port;
  u16 family;

  RemoteAddress() : addr(0), port(0), family(0) {}
};

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

struct Security {
  u32 prize_seed = 0;
  u32 door_seed = 0;
  u32 timestamp = 0;
  u32 checksum_key = 0;
};

struct Connection {
  enum class TickResult { Success, ConnectionClosed, ConnectionError };

  SocketType fd = -1;
  RemoteAddress remote_addr;
  bool connected = false;
  MemoryArena& temp_arena;
  ContinuumEncrypt encrypt;

  PacketSequencer packet_sequencer;
  NetworkBuffer buffer;

  MapHandler map_handler;
  Security security;
  ArenaSettings settings;

  u32 packets_sent = 0;
  u32 packets_received = 0;
  u32 last_sync = 0;

  u16 player_id;

  Connection(MemoryArena& perm_arena, MemoryArena& temp_arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);

  size_t Send(u8* data, size_t size);
  size_t Send(NetworkBuffer& buffer);

  TickResult Tick();

  void ProcessPacket(u8* pkt, size_t size);

  void OnMapLoad();
  void SendSecurityPacket();
  void SendSyncTimeRequestPacket();
};

}  // namespace null

#endif
