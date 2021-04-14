#include "PlayerManager.h"

#include <cassert>
#include <cstdio>

#include "Buffer.h"
#include "Tick.h"
#include "net/Checksum.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Animation.h"

namespace null {

extern AnimatedSprite explosion_sprite;
extern AnimatedSprite warp_sprite;

static void OnPlayerIdPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnPlayerIdChange(pkt, size);
}

static void OnPlayerEnterPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnPlayerEnter(pkt, size);
}

static void OnPlayerLeavePkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnPlayerLeave(pkt, size);
}

static void OnPlayerFreqAndShipChangePkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnPlayerFreqAndShipChange(pkt, size);
}

static void OnLargePositionPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnLargePositionPacket(pkt, size);
}

static void OnSmallPositionPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnSmallPositionPacket(pkt, size);
}

static void OnPlayerDeathPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnPlayerDeath(pkt, size);
}

PlayerManager::PlayerManager(Connection& connection, PacketDispatcher& dispatcher) : connection(connection) {
  dispatcher.Register(ProtocolS2C::PlayerId, OnPlayerIdPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerLeaving, OnPlayerLeavePkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
  dispatcher.Register(ProtocolS2C::SmallPosition, OnSmallPositionPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerDeath, OnPlayerDeathPkt, this);
}

void PlayerManager::Update(float dt) {
  null::Tick current_tick = GetCurrentTick();

  s32 position_delay = 100;

  // TODO: varying delay based on movement
  Player* player = GetPlayerById(player_id);
  if (player && player->ship != 8) {
    position_delay = 10;
  }

  // Continuum client seems to send position update every second while spectating.
  // Subgame will kick the client if they stop sending packets for too long.
  if (connection.login_state == Connection::LoginState::Complete &&
      TICK_DIFF(current_tick, last_position_tick) >= position_delay) {
    SendPositionPacket();
    last_position_tick = current_tick;
  }
}

void PlayerManager::SendPositionPacket() {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  Player* player = GetPlayerById(player_id);

  assert(player);

  u16 x = (u16)(player->position.x * 16.0f);
  u16 y = (u16)(player->position.y * 16.0f);

  u16 vel_x = (u16)(player->velocity.x * 16.0f * 10.0f);
  u16 vel_y = (u16)(player->velocity.y * 16.0f * 10.0f);

  u16 weapon = *(u16*)&player->weapon;
  u16 energy = connection.settings.ShipSettings[player->ship].MaximumEnergy;
  s32 time_diff = connection.time_diff;

  buffer.WriteU8(0x03);                           // Type
  buffer.WriteU8(player->direction);              // Direction
  buffer.WriteU32(GetCurrentTick() + time_diff);  // Timestamp
  buffer.WriteU16(vel_x);                         // X velocity
  buffer.WriteU16(y);                             // Y
  buffer.WriteU8(0);                              // Checksum
  buffer.WriteU8(player->togglables);             // Togglables
  buffer.WriteU16(x);                             // X
  buffer.WriteU16(vel_y);                         // Y velocity
  buffer.WriteU16(player->bounty);                // Bounty
  buffer.WriteU16(energy);                        // Energy
  buffer.WriteU16(weapon);                        // Weapon info

  u8 checksum = WeaponChecksum(buffer.data, buffer.GetSize());
  buffer.data[10] = checksum;

  connection.Send(buffer);
}

Player* PlayerManager::GetSelf() { return GetPlayerById(player_id); }

Player* PlayerManager::GetPlayerById(u16 id, size_t* index) {
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

void PlayerManager::OnPlayerIdChange(u8* pkt, size_t size) {
  player_id = *(u16*)(pkt + 1);
  printf("Player id: %d\n", player_id);
}

void PlayerManager::OnPlayerEnter(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

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
  player->timestamp = GetCurrentTick() & 0xFFFF;
  player->lerp_time = 0.0f;
  player->warp_animation.sprite = &warp_sprite;
  player->warp_animation.t = warp_sprite.duration;
  player->explode_animation.sprite = &explosion_sprite;
  player->explode_animation.t = explosion_sprite.duration;
  player->enter_delay = 0.0f;

  printf("%s entered arena\n", name);
}

void PlayerManager::OnPlayerLeave(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u16 pid = buffer.ReadU16();

  size_t index;
  Player* player = GetPlayerById(pid, &index);

  if (player) {
    printf("%s left arena\n", player->name);

    players[index] = players[--player_count];
  }
}

void PlayerManager::OnPlayerDeath(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 green_id = buffer.ReadU8();
  u16 killer_id = buffer.ReadU16();
  u16 killed_id = buffer.ReadU16();

  Player* player = GetPlayerById(killed_id);
  if (player) {
    // Hide the player until they send a new position packet
    player->enter_delay = connection.settings.EnterDelay / 100.0f;
    player->explode_animation.t = 0.0f;
  }
}

void PlayerManager::OnPlayerFreqAndShipChange(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 ship = buffer.ReadU8();
  u16 pid = buffer.ReadU16();
  u16 freq = buffer.ReadU16();

  Player* player = GetPlayerById(pid);

  if (player) {
    player->ship = ship;
    player->frequency = freq;

    // Hide the player until they send a new position packet
    player->position = Vector2f(0, 0);
    player->velocity = Vector2f(0, 0);
    player->lerp_time = 0.0f;
    player->warp_animation.t = 0.0f;
  }
}

void PlayerManager::OnLargePositionPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 direction = buffer.ReadU8();
  u16 timestamp = buffer.ReadU16();
  u16 x = buffer.ReadU16();
  s16 vel_y = (s16)buffer.ReadU16();
  u16 pid = buffer.ReadU16();

  Player* player = GetPlayerById(pid);

  if (player) {
    player->direction = direction;
    player->velocity.y = vel_y / 16.0f / 10.0f;
    player->velocity.x = (s16)buffer.ReadU16() / 16.0f / 10.0f;
    u8 checksum = buffer.ReadU8();
    player->togglables = buffer.ReadU8();
    player->ping = buffer.ReadU8();
    u16 y = buffer.ReadU16();
    player->bounty = buffer.ReadU16();

    Vector2f pkt_position(x / 16.0f, y / 16.0f);
    // Put packet timestamp into local time
    player->timestamp = (timestamp - connection.time_diff) & 0xFFFF;
    s32 timestamp_diff = TICK_DIFF(GetCurrentTick(), (GetCurrentTick() & 0xFFFF0000) | player->timestamp);

    if (timestamp_diff > 15) {
      timestamp_diff = 15;
    } else if (timestamp_diff < -15) {
      timestamp_diff = -15;
    }

    player->ping += timestamp_diff;

    u16 weapon = buffer.ReadU16();
    memcpy(&player->weapon, &weapon, sizeof(weapon));

    if (weapon != 0) {
      ++connection.weapons_received;
    }

    OnPositionPacket(*player, pkt_position);
  }
}

void PlayerManager::OnSmallPositionPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

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
    player->togglables = buffer.ReadU8();
    player->velocity.y = (s16)buffer.ReadU16() / 16.0f / 10.0f;
    u16 y = buffer.ReadU16();
    player->velocity.x = (s16)buffer.ReadU16() / 16.0f / 10.0f;

    Vector2f pkt_position(x / 16.0f, y / 16.0f);
    // Put packet timestamp into local time
    player->timestamp = (timestamp - connection.time_diff) & 0xFFFF;
    s32 timestamp_diff = TICK_DIFF(GetCurrentTick(), (GetCurrentTick() & 0xFFFF0000) | player->timestamp);

    if (timestamp_diff > 15) {
      timestamp_diff = 15;
    } else if (timestamp_diff < -15) {
      timestamp_diff = -15;
    }

    player->ping += timestamp_diff;
    OnPositionPacket(*player, pkt_position);
  }
}

void PlayerManager::OnPositionPacket(Player& player, const Vector2f& position) {
  if (player.position == Vector2f(0, 0) && position != Vector2f(0, 0)) {
    player.warp_animation.t = 0.0f;
  }

  // TODO: Simulate through map
  Vector2f projected_pos = position + player.velocity * (player.ping / 100.0f);

  float abs_dx = abs(projected_pos.x - player.position.x);
  float abs_dy = abs(projected_pos.y - player.position.y);

  // Jump to the position if very out of sync
  if (abs_dx >= 4.0f || abs_dy >= 4.0f) {
    player.position = projected_pos;
    player.lerp_time = 0.0f;
  } else {
    player.lerp_time = 200.0f / 1000.0f;
    player.lerp_velocity = (projected_pos - player.position) * (1.0f / player.lerp_time);
  }
}

}  // namespace null
