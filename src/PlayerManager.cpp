#include "PlayerManager.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "Buffer.h"
#include "InputState.h"
#include "ShipController.h"
#include "Tick.h"
#include "WeaponManager.h"
#include "net/Checksum.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Animation.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

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

static void OnFlagClaimPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnFlagClaim(pkt, size);
}

static void OnFlagDropPkt(void* user, u8* pkt, size_t size) {
  PlayerManager* manager = (PlayerManager*)user;
  manager->OnFlagDrop(pkt, size);
}

inline bool IsPlayerVisible(Player* self, u32 self_freq, Player* player) {
  if (self_freq == player->frequency) return true;

  return (!(player->togglables & Status_Cloak)) || (self->togglables & Status_XRadar);
}

PlayerManager::PlayerManager(Connection& connection, PacketDispatcher& dispatcher) : connection(connection) {
  dispatcher.Register(ProtocolS2C::PlayerId, OnPlayerIdPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerLeaving, OnPlayerLeavePkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
  dispatcher.Register(ProtocolS2C::SmallPosition, OnSmallPositionPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerDeath, OnPlayerDeathPkt, this);
  dispatcher.Register(ProtocolS2C::FlagClaim, OnFlagClaimPkt, this);
  dispatcher.Register(ProtocolS2C::DropFlag, OnFlagDropPkt, this);
}

void PlayerManager::Update(float dt) {
  null::Tick current_tick = GetCurrentTick();
  Player* self = GetPlayerById(player_id);

  if (!self) return;

  for (size_t i = 0; i < this->player_count; ++i) {
    Player* player = this->players + i;

    if (player->ship == 8) continue;

    SimulatePlayer(*player, dt);

    player->explode_animation.t += dt;
    player->warp_animation.t += dt;

    if (player->enter_delay > 0.0f) {
      player->enter_delay -= dt;

      if (!player->explode_animation.IsAnimating()) {
        if (player != self) {
          player->position = Vector2f(0, 0);
          player->lerp_time = 0.0f;
        }

        player->velocity = Vector2f(0, 0);
      }

      if (player == self && player->enter_delay <= 0.0f) {
        Spawn();
        player->warp_animation.t = 0.0f;
      }
    }
  }

  s32 position_delay = 100;

  if (self && self->ship != 8) {
    position_delay = 10;
  }

  // Continuum client seems to send position update every second while spectating.
  // Subgame will kick the client if they stop sending packets for too long.
  if (connection.login_state == Connection::LoginState::Complete &&
      TICK_DIFF(current_tick, last_position_tick) >= position_delay) {
    SendPositionPacket();
  }
}

void PlayerManager::Render(Camera& camera, SpriteRenderer& renderer, u32 self_freq) {
  Player* self = GetPlayerById(player_id);

  if (!self) return;

  // Draw player ships
  for (size_t i = 0; i < this->player_count; ++i) {
    Player* player = this->players + i;

    if (player->ship == 8) continue;
    if (player->position == Vector2f(0, 0)) continue;

    if (player->explode_animation.IsAnimating()) {
      SpriteRenderable& renderable = player->explode_animation.GetFrame();
      Vector2f position = player->position - renderable.dimensions * (0.5f / 16.0f);

      renderer.Draw(camera, renderable, position, Layer::AfterShips);
    } else if (player->enter_delay <= 0.0f) {
      if (IsPlayerVisible(self, self_freq, player)) {
        size_t index = player->ship * 40 + (u8)(player->orientation * 40.0f);

        Vector2f offset = Graphics::ship_sprites[index].dimensions * (0.5f / 16.0f);
        Vector2f position = player->position.PixelRounded() - offset.PixelRounded();

        renderer.Draw(camera, Graphics::ship_sprites[index], position, Layer::Ships);
      }
      if (player->warp_animation.IsAnimating()) {
        SpriteRenderable& renderable = player->warp_animation.GetFrame();
        Vector2f position = player->position - renderable.dimensions * (0.5f / 16.0f);

        renderer.Draw(camera, renderable, position, Layer::AfterShips);
      }
    } else if (player == self && player->enter_delay > 0 && !player->explode_animation.IsAnimating()) {
      char output[256];
      sprintf(output, "%.1f", player->enter_delay);
      renderer.DrawText(camera, output, TextColor::DarkRed, camera.position, Layer::TopMost, TextAlignment::Center);
    }
  }

  // Draw player names - This is done in separate loop to batch sprite sheet renderables
  for (size_t i = 0; i < this->player_count; ++i) {
    Player* player = this->players + i;

    if (player->ship == 8) continue;
    if (player->position == Vector2f(0, 0)) continue;
    if (!IsPlayerVisible(self, self_freq, player)) continue;

    if (player->enter_delay <= 0.0f) {
      size_t index = player->ship * 40 + (u8)(player->orientation * 40.0f);
      Vector2f offset = Graphics::ship_sprites[index].dimensions * (0.5f / 16.0f);

      offset = offset.PixelRounded();

      char display[48];

      if (player->flags > 0) {
        sprintf(display, "%s(%d:%d)[%d]", player->name, player->bounty, player->flags, player->ping * 10);
      } else {
        sprintf(display, "%s(%d)[%d]", player->name, player->bounty, player->ping * 10);
      }

      TextColor color = TextColor::Blue;

      if (player->frequency == self_freq) {
        color = TextColor::Yellow;
      } else if (player->flags > 0) {
        color = TextColor::DarkRed;
      }

      Vector2f position = player->position.PixelRounded() + offset;

      float max_energy = (float)ship_controller->ship.energy;
      if (player->id == player_id && player->energy < max_energy * 0.5f) {
        TextColor energy_color = player->energy < max_energy * 0.25f ? TextColor::DarkRed : TextColor::Yellow;
        char energy_output[16];
        sprintf(energy_output, "%d", (u32)player->energy);
        renderer.DrawText(camera, energy_output, energy_color, position, Layer::AfterShips);
        position.y += (12.0f / 16.0f);
      } else if (player->id != player_id && player->energy > 0.0f) {
        char energy_output[16];
        sprintf(energy_output, "%d", (u32)player->energy);
        Vector2f energy_p = player->position.PixelRounded() + Vector2f(-0.5f, offset.y);

        renderer.DrawText(camera, energy_output, TextColor::Blue, energy_p, Layer::AfterShips, TextAlignment::Right);
      }

      renderer.DrawText(camera, display, color, position, Layer::AfterShips);
    }
  }
}

void PlayerManager::SendPositionPacket() {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  Player* player = GetPlayerById(player_id);

  assert(player);

  if (player->ship != 8 && player->energy <= 0) return;

  u16 x = (u16)(player->position.x * 16.0f);
  u16 y = (u16)(player->position.y * 16.0f);

  u16 vel_x = (u16)(player->velocity.x * 16.0f * 10.0f);
  u16 vel_y = (u16)(player->velocity.y * 16.0f * 10.0f);

  u16 weapon = *(u16*)&player->weapon;
  u16 energy = (u16)player->energy;
  s32 time_diff = connection.time_diff;

  u8 direction = (u8)(player->orientation * 40.0f);

  buffer.WriteU8(0x03);                           // Type
  buffer.WriteU8(direction);                      // Direction
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

  if (connection.extra_position_info) {
    buffer.WriteU16(energy);
    buffer.WriteU16(connection.ping);
    buffer.WriteU16(0);

    struct {
      u32 shields : 1;
      u32 super : 1;
      u32 bursts : 4;
      u32 repels : 4;
      u32 thors : 4;
      u32 bricks : 4;
      u32 decoys : 4;
      u32 rockets : 4;
      u32 portals : 4;
      u32 padding : 2;
    } item_info = {};

    item_info.bursts = ship_controller->ship.bursts;
    item_info.repels = ship_controller->ship.repels;
    item_info.thors = ship_controller->ship.thors;
    item_info.bricks = ship_controller->ship.bricks;
    item_info.decoys = ship_controller->ship.decoys;
    item_info.rockets = ship_controller->ship.rockets;
    item_info.portals = ship_controller->ship.portals;

    buffer.WriteU32(*(u32*)&item_info);
  }

  connection.Send(buffer);
  last_position_tick = GetCurrentTick();
  player->togglables &= ~Status_Flash;
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

  this->player_count = 0;
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
  player->warp_animation.sprite = &Graphics::anim_ship_warp;
  player->warp_animation.t = Graphics::anim_ship_warp.duration;
  player->explode_animation.sprite = &Graphics::anim_ship_explode;
  player->explode_animation.t = Graphics::anim_ship_explode.duration;
  player->enter_delay = 0.0f;
  player->last_bounce_tick = 0;

  memset(&player->weapon, 0, sizeof(player->weapon));

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
  u16 bounty = buffer.ReadU16();
  u16 flag_transfer = buffer.ReadU16();

  Player* killed = GetPlayerById(killed_id);
  Player* killer = GetPlayerById(killer_id);

  if (killed) {
    // Hide the player until they send a new position packet
    killed->enter_delay = (connection.settings.EnterDelay / 100.0f) + killed->explode_animation.sprite->duration;
    killed->explode_animation.t = 0.0f;
    killed->flags = 0;
  }

  if (killer) {
    killer->flags += flag_transfer;
    if (killer->id == player_id) {
      killer->bounty += connection.settings.BountyIncreaseForKill;
    }
  }
}

void PlayerManager::Spawn() {
  Player* self = GetSelf();

  if (!self) return;

  u8 ship = self->ship;

  // TODO: read correct frequency
  float x_center = abs((float)connection.settings.SpawnSettings[0].X);
  float y_center = abs((float)connection.settings.SpawnSettings[0].Y);
  int radius = connection.settings.SpawnSettings[0].Radius;

  if (x_center == 0) {
    x_center = 512;
  }
  if (y_center == 0) {
    y_center = 512;
  }

  if (radius == 0) {
    self->position = Vector2f(x_center, y_center);
  } else {
    // Try 100 times to spawn in a random spot. TODO: Improve this to find open space better
    for (int i = 0; i < 100; ++i) {
      float x_offset = (float)((rand() % (radius * 2)) - radius);
      float y_offset = (float)((rand() % (radius * 2)) - radius);

      Vector2f spawn(x_center + x_offset, y_center + y_offset);

      if (!connection.map.IsSolid((u16)spawn.x, (u16)spawn.y) || i == 99) {
        self->position = spawn;
        break;
      }
    }
  }

  ship_controller->ResetShip();
  self->togglables |= Status_Flash;
  SendPositionPacket();
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
    player->enter_delay = 0.0f;
    player->flags = 0;
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
    player->orientation = direction / 40.0f;
    player->velocity.y = vel_y / 16.0f / 10.0f;
    player->velocity.x = (s16)buffer.ReadU16() / 16.0f / 10.0f;

    u8 checksum = buffer.ReadU8();
    player->togglables = buffer.ReadU8();
    player->ping = buffer.ReadU8();
    u16 y = buffer.ReadU16();
    player->bounty = buffer.ReadU16();

    if (player->togglables & Status_Flash) {
      player->warp_animation.t = 0.0f;
    }

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

    if (size >= 23) {
      player->energy = (float)buffer.ReadU16();
    } else {
      player->energy = 0;
    }

    if (size >= 25) {
      player->s2c_latency = buffer.ReadU16();
    } else {
      player->s2c_latency = 0;
    }

    if (size >= 27) {
      player->timers = buffer.ReadU16();
    } else {
      player->timers = 0;
    }

    if (size >= 31) {
      player->items = buffer.ReadU32();
    } else {
      player->items = 0;
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
    player->orientation = direction / 40.0f;
    player->ping = ping;
    player->bounty = bounty;
    player->togglables = buffer.ReadU8();
    player->velocity.y = (s16)buffer.ReadU16() / 16.0f / 10.0f;
    u16 y = buffer.ReadU16();
    player->velocity.x = (s16)buffer.ReadU16() / 16.0f / 10.0f;

    if (player->togglables & Status_Flash) {
      player->warp_animation.t = 0.0f;
    }

    if (size >= 18) {
      player->energy = (float)buffer.ReadU16();
    } else {
      player->energy = 0.0f;
    }

    if (size >= 20) {
      player->s2c_latency = buffer.ReadU16();
    } else {
      player->s2c_latency = 0;
    }

    if (size >= 22) {
      player->timers = buffer.ReadU16();
    } else {
      player->timers = 0;
    }

    if (size >= 26) {
      player->items = buffer.ReadU32();
    } else {
      player->items = 0;
    }

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

  Vector2f previous_pos = player.position;

  // Hard set the new position so we can simulate from it to catch up to where the player would be now after ping ticks
  player.position = position;

  // Simulate per tick because the simulation can be unstable with large dt
  for (int i = 0; i < player.ping; ++i) {
    SimulatePlayer(player, (1.0f / 100.0f));
  }

  Vector2f projected_pos = player.position;

  // Set the player back to where they were before the simulation so they can be lerped to new position.
  player.position = previous_pos;

  float abs_dx = abs(projected_pos.x - player.position.x);
  float abs_dy = abs(projected_pos.y - player.position.y);

  // Jump to the position if very out of sync
  if (abs_dx >= 4.0f || abs_dy >= 4.0f) {
    player.position = projected_pos;
    player.lerp_time = 0.0f;
  } else {
    player.lerp_time = 200.0f / 1000.0f;
    player.lerp_velocity = (projected_pos - player.position.PixelRounded()) * (1.0f / player.lerp_time);
  }
}

void PlayerManager::OnFlagClaim(u8* pkt, size_t size) {
  u16 flag_id = *(u16*)(pkt + 1);
  u16 player_id = *(u16*)(pkt + 3);

  Player* player = GetPlayerById(player_id);

  if (player) {
    player->flags++;
  }
}

void PlayerManager::OnFlagDrop(u8* pkt, size_t size) {
  u16 player_id = *(u16*)(pkt + 1);

  Player* player = GetPlayerById(player_id);

  if (player) {
    if (player->flags > 0) {
      player->flags--;
    }
  }
}

bool PlayerManager::SimulateAxis(Player& player, float dt, int axis) {
  float bounce_factor = 16.0f / connection.settings.BounceFactor;
  Map& map = connection.map;

  int axis_flip = axis == 0 ? 1 : 0;
  float radius = connection.settings.ShipSettings[player.ship].GetRadius();
  float previous = player.position.values[axis];

  player.position.values[axis] += player.velocity.values[axis] * dt;
  float delta = player.velocity.values[axis] * dt;

  if (player.lerp_time > 0.0f) {
    float timestep = dt;
    if (player.lerp_time < timestep) {
      timestep = player.lerp_time;
    }

    player.position.values[axis] += player.lerp_velocity.values[axis] * timestep;
    delta += player.lerp_velocity.values[axis] * timestep;
  }

  u16 check = (u16)(player.position.values[axis] + radius);

  if (delta < 0) {
    check = (u16)(player.position.values[axis] - radius);
  }

  s16 start = (s16)(player.position.values[axis_flip] - radius - 1);
  s16 end = (s16)(player.position.values[axis_flip] + radius + 1);

  Vector2f collider_min = player.position.PixelRounded() - Vector2f(radius, radius);
  Vector2f collider_max = player.position.PixelRounded() + Vector2f(radius, radius);

  bool collided = check < 0 || check > 1023;
  for (s16 other = start; other < end && !collided; ++other) {
    // TODO: Handle special tiles like warp here

    if (axis == 0 && map.IsSolid(check, other)) {
      if (BoxBoxIntersect(collider_min, collider_max, Vector2f((float)check, (float)other),
                          Vector2f((float)check + 1, (float)other + 1))) {
        collided = true;
        break;
      }
    } else if (axis == 1 && map.IsSolid(other, check)) {
      if (BoxBoxIntersect(collider_min, collider_max, Vector2f((float)other, (float)check),
                          Vector2f((float)other + 1, (float)check + 1))) {
        collided = true;
        break;
      }
    }
  }

  if (collided) {
    u32 tick = GetCurrentTick();
    // Don't perform a bunch of wall slowdowns so the player doesn't get very slow against walls.
    if (TICK_DIFF(tick, player.last_bounce_tick) < 1) {
      bounce_factor = 1.0f;
    }

    player.position.values[axis] = previous;
    player.velocity.values[axis] *= -bounce_factor;
    player.velocity.values[axis_flip] *= bounce_factor;

    player.lerp_velocity.values[axis] *= -bounce_factor;
    player.lerp_velocity.values[axis_flip] *= bounce_factor;

    return true;
  }

  return false;
}

void PlayerManager::SimulatePlayer(Player& player, float dt) {
  bool x_bounce = SimulateAxis(player, dt, 0);
  bool y_bounce = SimulateAxis(player, dt, 1);

  if (x_bounce || y_bounce) {
    player.last_bounce_tick = GetCurrentTick();
  }

  player.lerp_time -= dt;
}

}  // namespace null
