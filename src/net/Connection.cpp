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

#include "../ArenaSettings.h"
#include "../Checksum.h"
#include "../Tick.h"
#include "Protocol.h"

//#define PACKET_SHEDDING 20

namespace null {

extern const char* kPlayerName;
extern const char* kPlayerPassword;

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

Connection::Connection(MemoryArena& perm_arena, MemoryArena& temp_arena)
    : remote_addr(),
      buffer(perm_arena, kMaxPacketSize),
      temp_arena(temp_arena),
      packet_sequencer(perm_arena, temp_arena),
      map_handler(perm_arena, temp_arena),
      last_sync_tick(GetCurrentTick()),
      last_position_tick(GetCurrentTick()) {}

Connection::TickResult Connection::Tick() {
  constexpr s32 kSyncDelay = 500;
  constexpr s32 kPositionDelay = 100;

  sockaddr_in addr = {};
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  null::Tick current_tick = GetCurrentTick();

  // Continuum client seems to send sync request every 5 seconds
  if (TICK_DIFF(current_tick, last_sync_tick) >= kSyncDelay) {
    SendSyncTimeRequestPacket(false);
  }

  // Continuum client seems to send position update every second while spectating.
  // Subgame will kick the client if they stop sending packets for too long.
  if (login_state == LoginState::Complete && TICK_DIFF(current_tick, last_position_tick) >= kPositionDelay) {
    SendPositionPacket();
    last_position_tick = current_tick;
  }

  packet_sequencer.Tick(*this);
  // Buffer must be reset after packet sequencer runs so the read pointer is reset.
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

    ++packets_received;
    buffer.write += bytes_recv;

    u8* pkt = (u8*)buffer.data;
    size_t size = bytes_recv;

    if (encrypt.key1 != 0 || encrypt.key2 != 0) {
      size = encrypt.Decrypt(pkt, size);
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

  return TickResult::Success;
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
        // Send password packet now
        u8 data[kMaxPacketSize];
        NetworkBuffer buffer(data, kMaxPacketSize);

        char name[32] = {};
        char password[32] = {};

        strcpy(name, kPlayerName);
        strcpy(password, kPlayerPassword);

        buffer.WriteU8(0x24);  // Continuum password packet
        buffer.WriteU8(0x00);  // New user
        buffer.WriteString(name, 32);
        buffer.WriteString(password, 32);
        buffer.WriteU32(1178436307);  // Machine ID
        buffer.WriteU8(0x00);         // connect type
        buffer.WriteU16(240);         // Time zone bias
        buffer.WriteU16(0);           // Unknown
        buffer.WriteU16(40);          // Version
        buffer.WriteU32(0xA5);
        buffer.WriteU32(0x00);
        buffer.WriteU32(0);  // permission id

        buffer.WriteU32(0);
        buffer.WriteU32(0);
        buffer.WriteU32(0);

        for (int i = 0; i < 16; ++i) {
          buffer.WriteU32(0);
        }

        packet_sequencer.SendReliableMessage(*this, buffer.data, buffer.GetSize());
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
        u32 rtt = GetCurrentTick() - sent_timestamp;

        ping = (u32)((rtt / 2.0f) * 10.0f);
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
        printf("Received unhandled core packet of type 0x%02X\n", type);
      } break;
    }
  } else {
    assert(type_byte < (u8)ProtocolS2C::Count);
    ProtocolS2C type = (ProtocolS2C)type_byte;

    switch (type) {
      case ProtocolS2C::PlayerId: {
        player_id = buffer.ReadU16();
        printf("Player id: %d\n", player_id);
      } break;
      case ProtocolS2C::JoinGame: {
        printf("Successfully joined game.\n");
      } break;
      case ProtocolS2C::PlayerEntering: {
        Player* player = players + player_count++;

        player->ship = buffer.ReadU8();
        u8 audio = buffer.ReadU8();
        char* name = buffer.ReadString(20);
        char* squad = buffer.ReadString(20);

        memcpy(player->name, name, 20);
        memcpy(player->squad, squad, 20);

        player->kill_points = buffer.ReadU32();
        player->flag_points = buffer.ReadU32();
        player->id = buffer.ReadU16();
        player->frequency = buffer.ReadU16();
        player->wins = buffer.ReadU16();
        player->losses = buffer.ReadU16();
        player->attach_parent = buffer.ReadU16();
        player->flags = buffer.ReadU16();
        player->koth = buffer.ReadU8();

        printf("%s entered arena\n", name);

        if (buffer.read < buffer.write) {
          ProcessPacket(buffer.read, (size_t)(buffer.write - buffer.read));
        }
      } break;
      case ProtocolS2C::PlayerLeaving: {
        u16 pid = buffer.ReadU16();

        size_t index;
        Player* player = GetPlayerById(pid, &index);

        if (player) {
          printf("%s left arena\n", player->name);

          players[index] = players[--player_count];
        }
      } break;
      case ProtocolS2C::LargePosition: {
        u8 direction = buffer.ReadU8();
        u16 timestamp = buffer.ReadU16();
        u16 x = buffer.ReadU16();
        u16 vel_y = buffer.ReadU16();
        u16 pid = buffer.ReadU16();

        Player* player = GetPlayerById(pid);

        if (player) {
          player->direction = direction;
          player->position.x = x / 16.0f;
          player->velocity.y = vel_y / 16.0f;

          player->velocity.x = buffer.ReadU16() / 16.0f;
          u8 checksum = buffer.ReadU8();
          player->togglables = buffer.ReadU8();
          player->ping = buffer.ReadU8();
          player->position.y = buffer.ReadU16() / 16.0f;
          player->bounty = buffer.ReadU16();

          u16 weapon = buffer.ReadU16();
          memcpy(&player->weapon, &weapon, sizeof(weapon));
        }
      } break;
      case ProtocolS2C::PlayerDeath: {
      } break;
      case ProtocolS2C::Chat: {
        u8 type = buffer.ReadU8();
        u8 sound = buffer.ReadU8();
        u16 sender_id = buffer.ReadU16();

        size_t len = (size_t)(buffer.write - buffer.read);
        char* mesg = buffer.ReadString(len);

        Player* player = GetPlayerById(sender_id);

        if (player) {
          if (type == 0x03) {  // Team
            printf("T  ");
          } else if (type == 0x05) {  // Private
            printf("P  ");
          }

          printf("%s> ", player->name);
        }

        printf("%s\n", mesg);
      } break;
      case ProtocolS2C::PlayerPrize: {
      } break;
      case ProtocolS2C::Password: {
        u8 response = buffer.ReadU8();

        printf("Login response: %s\n", kLoginResponses[response]);

        if (response == 0x00) {
          u8 data[kMaxPacketSize];
          NetworkBuffer write(data, kMaxPacketSize);

          char arena[16] = {};

          // Join arena request
          write.WriteU8(0x01);     // type
          write.WriteU8(0x08);     // ship number
          write.WriteU16(0x00);    // allow audio
          write.WriteU16(1920);    // x res
          write.WriteU16(1080);    // y res
          write.WriteU16(0xFFFF);  // Arena number
          write.WriteString(arena, 16);

          packet_sequencer.SendReliableMessage(*this, write.read, write.GetSize());
          login_state = LoginState::ArenaLogin;
        }
      } break;
      case ProtocolS2C::CreateTurret: {
      } break;
      case ProtocolS2C::ArenaSettings: {
        // Settings struct contains the packet type data
        ArenaSettings* settings = (ArenaSettings*)(pkt);

        this->settings = *settings;
      } break;
      case ProtocolS2C::Security: {
        security.prize_seed = buffer.ReadU32();
        security.door_seed = buffer.ReadU32();
        security.timestamp = buffer.ReadU32();
        security.checksum_key = buffer.ReadU32();

        if (security.checksum_key && map_handler.checksum) {
          SendSecurityPacket();
        }
      } break;
      case ProtocolS2C::FlagPosition: {
      } break;
      case ProtocolS2C::FlagClaim: {
      } break;
      case ProtocolS2C::DropFlag: {
      } break;
      case ProtocolS2C::TeamAndShipChange: {
        u8 ship = buffer.ReadU8();
        u16 pid = buffer.ReadU16();
        u16 freq = buffer.ReadU16();

        Player* player = GetPlayerById(pid);

        if (player) {
          player->ship = ship;
          player->frequency = freq;
        }
      } break;
      case ProtocolS2C::BrickDropped: {
      } break;
      case ProtocolS2C::KeepAlive: {
      } break;
      case ProtocolS2C::SmallPosition: {
        u8 direction = buffer.ReadU8();
        u16 timestamp = buffer.ReadU16();
        u16 x = buffer.ReadU16();
        u8 ping = buffer.ReadU8();
        u8 bounty = buffer.ReadU8();
        u16 pid = buffer.ReadU8();

        Player* player = GetPlayerById(pid);

        if (player) {
          player->direction = direction;
          player->ping = ping;
          player->bounty = bounty;
          player->position.x = x / 16.0f;
          player->togglables = buffer.ReadU8();
          player->velocity.y = buffer.ReadU16() / 16.0f;
          player->position.y = buffer.ReadU16() / 16.0f;
          player->velocity.x = buffer.ReadU16() / 16.0f;
        }
      } break;
      case ProtocolS2C::MapInformation: {
        login_state = LoginState::MapDownload;

        if (map_handler.OnMapInformation(*this, pkt, size)) {
          OnMapLoad();
        }
      } break;
      case ProtocolS2C::CompressedMap: {
        if (map_handler.OnCompressedMap(*this, pkt, size)) {
          OnMapLoad();
        }
      } break;
      case ProtocolS2C::PowerballPosition: {
      } break;
      case ProtocolS2C::Version: {
        u16 version = buffer.ReadU16();
        u32 checksum = buffer.ReadU32();

        printf("Connected to server running Continuum 0.%d with checksum %08X\n", version, checksum);
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
        printf("Received unhandled non-core packet of type 0x%02X\n", type);
      } break;
    }
  }
}

// TODO: Move into player manager
Player* Connection::GetPlayerById(u16 id, size_t* index) {
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;

    if (player->id == id) {
      if (index) {
        *index = i;
      }
      return player;
    }
  }

  return nullptr;
}

void Connection::OnMapLoad() {
  // I don't think this should ever be set because the server doesn't know that the map was loaded yet
  if (security.checksum_key) {
    SendSecurityPacket();
  }

  login_state = LoginState::Complete;

  // Send a position packet to let server know that the map is loaded.
  SendPositionPacket();
  printf("Map loaded. Sending initial position packet\n");
}

void Connection::SendPositionPacket() {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  buffer.WriteU8(0x03);               // Type
  buffer.WriteU8(0);                  // Direction
  buffer.WriteU32(GetCurrentTick());  // Timestamp
  buffer.WriteU16(0);                 // X velocity
  buffer.WriteU16(0);                 // Y
  buffer.WriteU8(0);                  // Checksum
  buffer.WriteU8(0);                  // Togglables
  buffer.WriteU16(0);                 // X
  buffer.WriteU16(0);                 // Y velocity
  buffer.WriteU16(0);                 // Bounty
  buffer.WriteU16(0);                 // Energy
  buffer.WriteU16(0);                 // Weapon info

  u8 checksum = WeaponChecksum(buffer.data, buffer.GetSize());
  buffer.data[10] = checksum;

  Send(buffer);
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
  const Map& map = map_handler.map;

  u32 settings_checksum = SettingsChecksum(security.checksum_key, settings);
  u32 exe_checksum = MemoryChecksumGenerator::Generate(security.checksum_key);
  u32 map_checksum = map.GetChecksum(security.checksum_key);

  printf("Sending security packet with checksum seed %08X\n", security.checksum_key);

  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  buffer.WriteU8(0x1A);
  buffer.WriteU32(0x00);  // Weapon count
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

  sockaddr_in addr;
  addr.sin_family = remote_addr.family;
  addr.sin_port = remote_addr.port;
  addr.sin_addr.s_addr = remote_addr.addr;

  if (encrypt.key1 || encrypt.key2) {
    // Allocate enough space for both the crc and possibly the crc escape
    u8* dest = temp_arena.Allocate(size + 2);
    size = encrypt.Encrypt(data, dest, size);
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
