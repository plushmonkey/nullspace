#include "Connection.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>
#define WSAEWOULDBLOCK EWOULDBLOCK
#define closesocket close
#endif

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../ArenaSettings.h"
#include "../Platform.h"
#include "../Tick.h"
#include "Checksum.h"
#include "Protocol.h"

//#define PACKET_SHEDDING 20

namespace null {

extern const char* kPlayerName;
extern const char* kPlayerPassword;

#ifdef __ANDROID__
constexpr bool kDownloadLvz = false;
#else
constexpr bool kDownloadLvz = true;
#endif

const char* kLoginResponses[] = {"Ok",
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
                                 "Restricted zone, mod access required"};

inline int GetLastError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

static void OnDownloadComplete(void* user, struct FileRequest* request, u8* data) {
  Connection* connection = (Connection*)user;

  connection->OnDownloadComplete(request, data);
}

Connection::Connection(MemoryArena& perm_arena, MemoryArena& temp_arena, PacketDispatcher& dispatcher)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      dispatcher(dispatcher),
      remote_addr(),
      requester(perm_arena, temp_arena, *this, dispatcher),
      packet_sequencer(perm_arena, temp_arena),
      buffer(perm_arena, kMaxPacketSize),
      last_sync_tick(GetCurrentTick()),
      last_position_tick(GetCurrentTick()) {
  map_arena = perm_arena.CreateArena(Megabytes(6));
}

Connection::TickResult Connection::Tick() {
  constexpr s32 kSyncDelay = 500;

  sockaddr_in addr = {};
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  null::Tick current_tick = GetCurrentTick();

  // Continuum client seems to send sync request every 5 seconds
  if (TICK_DIFF(current_tick, last_sync_tick) >= kSyncDelay) {
    SendSyncTimeRequestPacket(false);
  }

  while (true) {
    packet_sequencer.Tick(*this);
    // Buffer must be reset after packet sequencer runs so the read pointer is reset.
    buffer.Reset();

    socklen_t socklen = sizeof(addr);
    int bytes_recv = recvfrom(fd, (char*)buffer.read, kMaxPacketSize, 0, (sockaddr*)&addr, &socklen);

    if (bytes_recv == 0) {
      this->connected = false;
      return TickResult::ConnectionClosed;
    } else if (bytes_recv < 0) {
      int err = GetLastError();

      if (err == WSAEWOULDBLOCK) {
        return TickResult::Success;
      }

      if (login_state != LoginState::Quit) {
        fprintf(stderr, "Unexpected socket error: %d\n", err);
        this->Disconnect();
      }
      return TickResult::ConnectionError;
    } else if (bytes_recv > 0) {
      assert(bytes_recv <= kMaxPacketSize);

      ++packets_received;
      buffer.write += bytes_recv;

      u8* pkt = (u8*)buffer.data;
      size_t size = bytes_recv;

      if (encrypt_method == EncryptMethod::Continuum && (encrypt.key1 != 0 || encrypt.key2 != 0)) {
        size = encrypt.Decrypt(pkt, size);
      } else if (encrypt_method == EncryptMethod::Subspace) {
        size = vie_encrypt.Decrypt(pkt, size);
      }

#ifdef PACKET_SHEDDING  // packet shedding for testing sequencer
      srand(GetCurrentTick());

      if (rand() % 100 > PACKET_SHEDDING) {
#else
      if (1) {
#endif
        ProcessPacket(pkt, size);
      }
    }
  }

  return TickResult::Success;
}

void Connection::SendPassword(bool registration) {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  char name[32] = {};
  char password[32] = {};

  u32 version = 40;
  u8 packet_type = 0x24;

  if (encrypt_method == EncryptMethod::Subspace) {
    version = 0x86;
    packet_type = 0x09;
  }

  strcpy(name, kPlayerName);
  strcpy(password, kPlayerPassword);

  buffer.WriteU8(packet_type);                 // Continuum password packet
  buffer.WriteU8(registration ? 0x01 : 0x00);  // New user
  buffer.WriteString(name, 32);
  buffer.WriteString(password, 32);
  buffer.WriteU32(1178436307);  // Machine ID
  buffer.WriteU8(0x00);         // connect type
  buffer.WriteU16(240);         // Time zone bias
  buffer.WriteU16(0);           // Unknown
  buffer.WriteU16(version);     // Version
  buffer.WriteU32(0xA5);
  buffer.WriteU32(0x00);
  buffer.WriteU32(0);  // permission id

  buffer.WriteU32(0);
  buffer.WriteU32(0);
  buffer.WriteU32(0);

  if (encrypt_method == EncryptMethod::Continuum) {
    for (int i = 0; i < 16; ++i) {
      buffer.WriteU32(0);
    }
  }

  packet_sequencer.SendReliableMessage(*this, buffer.data, buffer.GetSize());
}

void Connection::ProcessPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  u8 type_byte = buffer.ReadU8();

  if (type_byte == 0x00) {  // Core packet
    type_byte = buffer.ReadU8();

    assert(type_byte < (u8)ProtocolCore::Count);

    ProtocolCore type = (ProtocolCore)type_byte;

    switch (type) {
      case ProtocolCore::EncryptionResponse: {
        if (encrypt_method == EncryptMethod::Subspace) {
          if (!vie_encrypt.Initialize(*(u32*)(pkt + 2))) {
            fprintf(stderr, "Failed to initialize vie encryption.\n");
          }
        }

        SendPassword(false);

        login_state = LoginState::Authentication;

        SendSyncTimeRequestPacket(true);
      } break;
      case ProtocolCore::ReliableMessage: {
        packet_sequencer.OnReliableMessage(*this, pkt, size);
      } break;
      case ProtocolCore::ReliableAck: {
        packet_sequencer.OnReliableAck(*this, pkt, size);
      } break;
      case ProtocolCore::SyncTimeRequest: {
        u32 timestamp = buffer.ReadU32();

        struct {
          u8 core;
          u8 type;
          u32 received_timestamp;
          u32 local_timestamp;
        } sync_response = {0x00, 0x06, timestamp, GetCurrentTick()};

        packet_sequencer.SendReliableMessage(*this, (u8*)&sync_response, sizeof(sync_response));

        last_sync_tick = GetCurrentTick();
      } break;
      case ProtocolCore::SyncTimeResponse: {
        // The timestamp that was sent in the sync request
        u32 sent_timestamp = buffer.ReadU32();
        // The server timestamp at the time of request
        u32 server_timestamp = buffer.ReadU32();
        u32 current_tick = GetCurrentTick();
        u32 rtt = current_tick - sent_timestamp;

        ping = (u32)((rtt / 2.0f) * 10.0f);
        time_diff = ((rtt * 3) / 5) + server_timestamp - current_tick;
        if (time_diff >= -10 && time_diff <= 10) {
          time_diff = 0;
        }
      } break;
      case ProtocolCore::Disconnect: {
        this->connected = false;
      } break;
      case ProtocolCore::SmallChunkBody: {
        packet_sequencer.OnSmallChunkBody(*this, pkt, size);
      } break;
      case ProtocolCore::SmallChunkTail: {
        packet_sequencer.OnSmallChunkTail(*this, pkt, size);
      } break;
      case ProtocolCore::HugeChunk: {
        packet_sequencer.OnHugeChunk(*this, pkt, size);
      } break;
      case ProtocolCore::HugeChunkCancel: {
        packet_sequencer.OnCancelHugeChunk(*this, pkt, size);
      } break;
      case ProtocolCore::PacketCluster: {
        while (buffer.read < buffer.write) {
          u8 cluster_pkt_size = buffer.ReadU8();

          ProcessPacket(buffer.read, cluster_pkt_size);

          buffer.read += cluster_pkt_size;
        }
      } break;
      case ProtocolCore::ContinuumEncryptionResponse: {
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
      case ProtocolCore::ContinuumKeyExpansionRequest: {
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
        printf("Received unhandled core packet of type 0x%02X\n", (int)type);
      } break;
    }
  } else {
    assert(type_byte < (u8)ProtocolS2C::Count);
    ProtocolS2C type = (ProtocolS2C)type_byte;

    switch (type) {
      case ProtocolS2C::PlayerId: {
        this->login_state = LoginState::ArenaLogin;
      } break;
      case ProtocolS2C::JoinGame: {
        printf("Successfully joined game.\n");
      } break;
      case ProtocolS2C::PlayerEntering: {
        // Skip the entire packet so the next one can be read if it exists
        buffer.ReadString(63);

        if (buffer.read < buffer.write) {
          ProcessPacket(buffer.read, (size_t)(buffer.write - buffer.read));
        }
      } break;
      case ProtocolS2C::PlayerLeaving: {
      } break;
      case ProtocolS2C::LargePosition: {
      } break;
      case ProtocolS2C::PlayerDeath: {
      } break;
      case ProtocolS2C::Chat: {
      } break;
      case ProtocolS2C::PlayerPrize: {
      } break;
      case ProtocolS2C::PasswordResponse: {
        u8 response = buffer.ReadU8();

        printf("Login response: %s\n", kLoginResponses[response]);

        u8 register_request = pkt[19];

        if (register_request) {
          printf("Registration form requested.\n");

          // Should display registration form here then send this once complete.
          login_state = LoginState::Registering;
#if 0
          u8 data[766];
          NetworkBuffer registration(data, NULLSPACE_ARRAY_SIZE(data), 0);

          char strbuffer[64] = {};

          registration.WriteU8(0x17);
          registration.WriteString(strbuffer, 32);  // Real name
          strcpy(strbuffer, "a@a.com");
          registration.WriteString(strbuffer, 64);  // Email
          memset(strbuffer, 0, 64);
          registration.WriteString(strbuffer, 32);  // City
          registration.WriteString(strbuffer, 24);  // State
          registration.WriteU8('M');                // Sex
          registration.WriteU8(0);                  // Age
          registration.WriteU8(1);                  // Connecting from home
          registration.WriteU8(0);                  // Work
          registration.WriteU8(0);                  // School
          registration.WriteU32(0);                 // Processor type
          registration.WriteU32(0);                 // Unknown
          registration.WriteString(strbuffer, 40);  // Real name windows registration
          registration.WriteString(strbuffer, 40);  // Organization

          for (size_t i = 0; i < 13; ++i) {
            registration.WriteString(strbuffer, 40); // Drivers
          }

          Send(registration);
#endif
        }

        if (response == 0x00 || response == 0x0D) {
          SendArenaLogin(8, 0, 1920, 1080, 0xFFFF, "");
          login_state = LoginState::ArenaLogin;
        } else if (response == 0x01) {
          // Send password packet again requesting the registration form
          SendPassword(true);
        }
      } break;
      case ProtocolS2C::CreateTurret: {
      } break;
      case ProtocolS2C::ArenaSettings: {
        // Settings struct contains the packet type data
        ArenaSettings* settings = (ArenaSettings*)(pkt);

        this->settings = *settings;

        if (settings->DoorMode >= 0) {
          // Force update
          map.last_seed_tick = GetCurrentTick() - settings->DoorDelay;
          map.UpdateDoors(*settings);
        }

        u8* weights = (u8*)&settings->PrizeWeights;
        this->prize_weight_total = 0;
        for (size_t i = 0; i < sizeof(settings->PrizeWeights); ++i) {
          this->prize_weight_total += weights[i];
        }
      } break;
      case ProtocolS2C::Security: {
        security.prize_seed = buffer.ReadU32();
        security.door_seed = buffer.ReadU32();
        security.timestamp = buffer.ReadU32();
        security.checksum_key = buffer.ReadU32();

        map.door_rng.Seed(security.door_seed);
        map.last_seed_tick = security.timestamp - time_diff;

        if (security.checksum_key && map.checksum) {
          SendSecurityPacket();
        }
      } break;
      case ProtocolS2C::FlagPosition: {
      } break;
      case ProtocolS2C::FlagClaim: {
      } break;
      case ProtocolS2C::DestroyTurret: {
      } break;
      case ProtocolS2C::DropFlag: {
      } break;
      case ProtocolS2C::Spectate: {
        if (size == 2) {
          extra_position_info = buffer.ReadU8() != 0;
        }
      } break;
      case ProtocolS2C::TeamAndShipChange: {
      } break;
      case ProtocolS2C::PlayerBannerChange: {
      } break;
      case ProtocolS2C::BrickDropped: {
      } break;
      case ProtocolS2C::KeepAlive: {
      } break;
      case ProtocolS2C::SmallPosition: {
      } break;
      case ProtocolS2C::MapInformation: {
        login_state = LoginState::MapDownload;

        char* raw_filename = buffer.ReadString(16);

        map.checksum = buffer.ReadU32();

        if (encrypt_method == EncryptMethod::Continuum) {
          map.compressed_size = buffer.ReadU32();
        }

        char filename[17];
        memcpy(filename, raw_filename, 16);
        filename[16] = 0;

        requester.Request(filename, 0, map.compressed_size, map.checksum, true, null::OnDownloadComplete, this);
      } break;
      case ProtocolS2C::CompressedMap: {
      } break;
      case ProtocolS2C::PowerballPosition: {
      } break;
      case ProtocolS2C::Version: {
        u16 version = buffer.ReadU16();
        u32 checksum = buffer.ReadU32();

        printf("Connected to server running Continuum 0.%d with checksum %08X\n", version, checksum);
      } break;
      case ProtocolS2C::SetCoordinates: {
      } break;
      case ProtocolS2C::ToggleLVZ: {
      } break;
      case ProtocolS2C::ModifyLVZ: {
      } break;
      case ProtocolS2C::ToggleSendDamage: {
      } break;
      case ProtocolS2C::WatchDamage: {
      } break;
      default: {
        printf("Received unhandled non-core packet of type 0x%02X\n", (int)type);
      } break;
    }
  }

  dispatcher.Dispatch(pkt, size);
}

void Connection::SendArenaLogin(u8 ship, u16 audio, u16 xres, u16 yres, u16 arena_number, const char* arena_name) {
  u8 data[kMaxPacketSize];
  NetworkBuffer write(data, kMaxPacketSize);

  // Join arena request
  write.WriteU8(0x01);           // type
  write.WriteU8(ship);           // ship number
  write.WriteU16(audio);         // allow audio
  write.WriteU16(xres);          // x res
  write.WriteU16(yres);          // y res
  write.WriteU16(arena_number);  // Arena number
  write.WriteString(arena_name, 16);
  write.WriteU8(kDownloadLvz);

  packet_sequencer.SendReliableMessage(*this, write.read, write.GetSize());
  login_state = LoginState::ArenaLogin;
}

void Connection::OnDownloadComplete(struct FileRequest* request, u8* data) {
  map_arena.Reset();

  if (!map.Load(map_arena, request->filename)) {
    fprintf(stderr, "Failed to load map %s.\n", request->filename);
    return;
  }

  map.door_rng.Seed(security.door_seed);
  map.last_seed_tick = security.timestamp - time_diff;

  if (security.checksum_key) {
    SendSecurityPacket();
  }

  login_state = LoginState::Complete;
}

void Connection::SendSyncTimeRequestPacket(bool reliable) {
#pragma pack(push, 1)
  struct {
    u8 core;
    u8 type;
    u32 timestamp;
    u32 total_sent;
    u32 total_received;
  } sync_request = {0, 0x05, GetCurrentTick(), packets_sent, packets_received};
#pragma pack(pop)

  if (reliable) {
    packet_sequencer.SendReliableMessage(*this, (u8*)&sync_request, sizeof(sync_request));
  } else {
    Send((u8*)&sync_request, sizeof(sync_request));
  }

  last_sync_tick = GetCurrentTick();
}

void Connection::SendSecurityPacket() {
  u32 settings_checksum = SettingsChecksum(security.checksum_key, settings);
  u32 map_checksum = map.GetChecksum(security.checksum_key);

  u32 exe_checksum = 0;
  if (encrypt_method == EncryptMethod::Continuum) {
    exe_checksum = MemoryChecksumGenerator::Generate(security.checksum_key);
  } else {
    exe_checksum = VieChecksum(security.checksum_key);
  }

  printf("Sending security packet with checksum seed %08X\n", security.checksum_key);

  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  buffer.WriteU8(0x1A);
  buffer.WriteU32(weapons_received);  // Weapon count
  buffer.WriteU32(settings_checksum);
  buffer.WriteU32(exe_checksum);
  buffer.WriteU32(map_checksum);
  buffer.WriteU32(0);     // S2C slow total
  buffer.WriteU32(0);     // S2C fast total
  buffer.WriteU16(0);     // S2C slow current
  buffer.WriteU16(0);     // S2C fast current
  buffer.WriteU16(0);     // S2C reliable out
  buffer.WriteU16(ping);  // Ping
  buffer.WriteU16(ping);  // Ping average
  buffer.WriteU16(ping);  // Ping low
  buffer.WriteU16(ping);  // Ping high
  buffer.WriteU8(0);      // slow frame

  packet_sequencer.SendReliableMessage(*this, buffer.data, buffer.GetSize());
}

void Connection::SendAttachRequest(u16 destination_pid) {
#pragma pack(push, 1)
  struct {
    u8 type;
    u16 player_id;
  } attach_request = {0x10, destination_pid};
#pragma pack(pop)

  packet_sequencer.SendReliableMessage(*this, (u8*)&attach_request, sizeof(attach_request));
}

void Connection::SendTakeGreen(u16 x, u16 y, s16 prize_id) {
  u32 timestamp = GetCurrentTick();

#pragma pack(push, 1)
  struct {
    u8 type;
    u32 timestamp;
    u16 x;
    u16 y;
    s16 prize_id;
  } pkt = {0x07, timestamp, x, y, prize_id};
#pragma pack(pop)

  if (settings.TakePrizeReliable) {
    packet_sequencer.SendReliableMessage(*this, (u8*)&pkt, sizeof(pkt));
  } else {
    Send((u8*)&pkt, sizeof(pkt));
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
#ifdef PACKET_SHEDDING
  if (rand() % 100 < PACKET_SHEDDING) return size;
#endif

  // TODO: This should be a proper system, but right now it just needs to handle registration form
  if (size > 520) {
    // Max size of data reduced by reliable header and small chunk header
    constexpr size_t kMaxSize = 520 - 6 - 2;
    assert(size <= kMaxSize * 2);

    ArenaSnapshot snapshot = temp_arena.GetSnapshot();

    u8* small_chunk_body = temp_arena.Allocate(520);

    small_chunk_body[0] = 0x00;
    small_chunk_body[1] = 0x08;
    memcpy(small_chunk_body + 2, data, kMaxSize);

    packet_sequencer.SendReliableMessage(*this, small_chunk_body, 520 - 6);

    size_t remaining = size - kMaxSize;
    u8* small_chunk_tail = temp_arena.Allocate(520);

    small_chunk_tail[0] = 0x00;
    small_chunk_tail[1] = 0x09;
    memcpy(small_chunk_tail + 2, data + kMaxSize, remaining);

    packet_sequencer.SendReliableMessage(*this, small_chunk_tail, remaining + 2);

    temp_arena.Revert(snapshot);

    return size;
  }

  sockaddr_in addr;
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  if (encrypt_method == EncryptMethod::Continuum && (encrypt.key1 || encrypt.key2)) {
    // Allocate enough space for both the crc and possibly the crc escape
    u8* dest = temp_arena.Allocate(size + 2);
    size = encrypt.Encrypt(data, dest, size);
    data = dest;
  } else if (encrypt_method == EncryptMethod::Subspace) {
    u8* dest = temp_arena.Allocate(size);
    size = vie_encrypt.Encrypt(data, dest, size);
    data = dest;
  }

  int bytes = sendto(this->fd, (const char*)data, (int)size, 0, (sockaddr*)&addr, sizeof(addr));
  ++packets_sent;

  if (bytes <= 0) {
    Disconnect();
    return 0;
  }

  return bytes;
}

void Connection::SendDisconnect() {
  struct {
    u8 core;
    u8 type;
  } disconnect = {0x00, 0x07};

  Send((u8*)&disconnect, sizeof(u16));
}

void Connection::SendEncryptionRequest(EncryptMethod method) {
  this->encrypt_method = method;

  u32 key = 0;
  u16 version = 0x11;

  if (method == EncryptMethod::Subspace) {
    key = VieEncrypt::GenerateKey();
    vie_encrypt.client_key = key;
    version = 0x01;
  }

#pragma pack(push, 1)
  struct {
    u8 core;
    u8 request;
    u32 key;
    u16 version;
  } request = {0x00, 0x01, key, version};
#pragma pack(pop)

  Send((u8*)&request, sizeof(request));
}

void Connection::SendSpectateRequest(u16 pid) {
#pragma pack(push, 1)
  struct {
    u8 type;
    u16 pid;
  } spectate_request = {0x08, pid};
#pragma pack(pop)

  packet_sequencer.SendReliableMessage(*this, (u8*)&spectate_request, sizeof(spectate_request));
}

void Connection::SendShipRequest(u8 ship) {
  struct {
    u8 type;
    u8 ship;
  } request = {0x18, ship};

  packet_sequencer.SendReliableMessage(*this, (u8*)&request, sizeof(request));
}

void Connection::SendDeath(u16 killer, u16 bounty) {
#pragma pack(push, 1)
  struct {
    u8 type;
    u16 killer_pid;
    u16 bounty;
  } death_pkt = {0x05, killer, bounty};
#pragma pack(pop)

  packet_sequencer.SendReliableMessage(*this, (u8*)&death_pkt, sizeof(death_pkt));
}

void Connection::SendFrequencyChange(u16 freq) {
#pragma pack(push, 1)
  struct {
    u8 type;
    u16 freq;
  } request = {0x0F, freq};
#pragma pack(pop)

  packet_sequencer.SendReliableMessage(*this, (u8*)&request, sizeof(request));
}

void Connection::Disconnect() {
  login_state = LoginState::Quit;
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
