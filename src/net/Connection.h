#ifndef NULLSPACE_CONNECTION_H_
#define NULLSPACE_CONNECTION_H_

#include <mutex>

#include "../ArenaSettings.h"
#include "../Buffer.h"
#include "../FileRequester.h"
#include "../Map.h"
#include "../Memory.h"
#include "../Settings.h"
#include "../Types.h"
#include "Crypt.h"
#include "PacketDispatcher.h"
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
  enum class LoginState {
    EncryptionRequested,  // Sent encryption request
    Authentication,       // Sent password packet
    Registering,
    ArenaLogin,  // Requested to join arena
    MapDownload,
    Complete,

    GameTimeout,

    Quit,
    ConnectTimeout,
  };

  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  MemoryArena map_arena;
  PacketDispatcher& dispatcher;

  SocketType fd = -1;
  RemoteAddress remote_addr;
  bool connected = false;
  EncryptMethod encrypt_method = EncryptMethod::Continuum;
  ContinuumEncrypt encrypt;
  VieEncrypt vie_encrypt;
  FileRequester requester;
  PacketSequencer packet_sequencer;
  NetworkBuffer buffer;
  std::mutex send_mutex;

  Vector2f view_dim;

  Map map;
  Security security;
  ArenaSettings settings = {};

  u32 connect_tick = 0;
  u32 login_tick = 0;
  u32 packets_sent = 0;
  u32 packets_received = 0;
  u32 weapons_received = 0;
  // GetCurrentTick() + time_diff = Server tick
  s32 time_diff = 0;
  u32 ping = 0;
  bool extra_position_info = false;

  u32 last_sync_tick = 0;
  u32 last_position_tick = 0;
  LoginState login_state = LoginState::EncryptionRequested;

  u32 prize_weight_total = 0;

  Connection(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, PacketDispatcher& dispatcher);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);

  size_t Send(u8* data, size_t size);
  size_t Send(NetworkBuffer& buffer);

  TickResult Tick();

  void ProcessPacket(u8* pkt, size_t size);

  void OnDownloadComplete(struct FileRequest* request, u8* data);
  void SendSecurityPacket();
  void SendSyncTimeRequestPacket(bool reliable);

  void SendDisconnect();
  void SendEncryptionRequest(EncryptMethod method);
  void SendSpectateRequest(u16 pid);
  void SendShipRequest(u8 ship);
  void SendDeath(u16 killer, u16 bounty);
  void SendFrequencyChange(u16 freq);
  void SendArenaLogin(u8 ship, u16 audio, u16 xres, u16 yres, u16 arena_number, const char* arena_name);
  void SendPassword(bool registration);
  void SendAttachRequest(u16 destination_pid);
  void SendAttachDrop();
  void SendTakeGreen(u16 x, u16 y, s16 prize_id);
  void SendFlagRequest(u16 flag_id);
  void SendFlagDrop();
  void SendDropBrick(const Vector2f& position);
};

}  // namespace null

#endif
