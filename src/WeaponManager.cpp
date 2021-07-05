#include "WeaponManager.h"

#include <cassert>
#include <chrono>
#include <cmath>

#include "Buffer.h"
#include "PlayerManager.h"
#include "Radar.h"
#include "ShipController.h"
#include "Sound.h"
#include "Tick.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

// TODO: Spatial partition acceleration structures

namespace null {

AudioType GetAudioType(ShipSettings& ship_settings, WeaponData data) {
  AudioType result = AudioType::None;

  switch (data.type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      size_t start = (size_t)AudioType::Gun1;

      result = (AudioType)(start + data.level);
    } break;
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      size_t start = (size_t)AudioType::Bomb1;

      if (ship_settings.EmpBomb) {
        start = (size_t)AudioType::EBomb1;
      } else if (data.alternate) {
        start = (size_t)AudioType::Mine1;
      }

      result = (AudioType)(start + data.level);
    } break;
    case WeaponType::Repel: {
      result = AudioType::Repel;
    } break;
    case WeaponType::Decoy: {
      result = AudioType::Decoy;
    } break;
    case WeaponType::Burst: {
      result = AudioType::Burst;
    } break;
    case WeaponType::Thor: {
      result = AudioType::Thor;
    } break;
    default: {
    } break;
  }

  return result;
}

static void OnLargePositionPkt(void* user, u8* pkt, size_t size) {
  WeaponManager* manager = (WeaponManager*)user;

  manager->OnWeaponPacket(pkt, size);
}

WeaponManager::WeaponManager(MemoryArena& temp_arena, Connection& connection, PlayerManager& player_manager,
                             PacketDispatcher& dispatcher, AnimationSystem& animation, SoundSystem& sound_system)
    : temp_arena(temp_arena),
      connection(connection),
      player_manager(player_manager),
      animation(animation),
      sound_system(sound_system) {
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
}

void WeaponManager::Update(float dt) {
  u32 tick = GetCurrentTick();

  link_removal_count = 0;

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    Player* player = player_manager.GetPlayerById(weapon->player_id);

    if (player && connection.map.GetTileId(player->position) == kTileSafeId) {
      weapons[i--] = weapons[--weapon_count];
      continue;
    }

    s32 tick_count = TICK_DIFF(tick, weapon->last_tick);

    for (s32 j = 0; j < tick_count; ++j) {
      WeaponSimulateResult result = WeaponSimulateResult::Continue;

      result = Simulate(*weapon);

      if (result != WeaponSimulateResult::Continue && weapon->link_id != kInvalidLink) {
        AddLinkRemoval(weapon->link_id, result);
      }

      if (result == WeaponSimulateResult::PlayerExplosion || result == WeaponSimulateResult::WallExplosion) {
        CreateExplosion(*weapon);
        weapons[i--] = weapons[--weapon_count];
        break;
      } else if (result == WeaponSimulateResult::TimedOut) {
        weapons[i--] = weapons[--weapon_count];
        break;
      }

      if (weapon->data.type == WeaponType::Bullet || weapon->data.type == WeaponType::BouncingBullet) {
        if (TICK_DIFF(weapon->last_tick, weapon->last_trail_tick) >= 2) {
          SpriteRenderable& frame = Graphics::anim_bullet_trails[weapon->data.level].frames[0];
          Vector2f offset = Vector2f(1 / 16.0f, 1 / 16.0f);
          // Spawn trails back slightly so they don't advance past the render position
          Vector2f position = (weapon->position - weapon->velocity * (1.0f / 100.0f) - offset).PixelRounded();

          animation.AddAnimation(Graphics::anim_bullet_trails[weapon->data.level], position)->layer = Layer::AfterTiles;
          weapon->last_trail_tick = weapon->last_tick;
        }
      } else if ((weapon->data.type == WeaponType::Bomb || weapon->data.type == WeaponType::ProximityBomb) &&
                 !weapon->data.alternate) {
        if (TICK_DIFF(weapon->last_tick, weapon->last_trail_tick) >= 5) {
          SpriteRenderable& frame = Graphics::anim_bomb_trails[weapon->data.level].frames[0];
          Vector2f offset = (frame.dimensions * (0.5f / 16.0f));
          Vector2f position = (weapon->position - offset).PixelRounded();

          animation.AddAnimation(Graphics::anim_bomb_trails[weapon->data.level], position)->layer = Layer::AfterTiles;
          weapon->last_trail_tick = weapon->last_tick;
        }
      }
    }
  }

  if (link_removal_count > 0) {
    for (size_t i = 0; i < weapon_count; ++i) {
      Weapon* weapon = weapons + i;

      if (weapon->link_id != kInvalidLink) {
        bool removed = false;

        for (size_t j = 0; j < link_removal_count; ++j) {
          WeaponLinkRemoval* removal = link_removals + j;

          if (removal->link_id == weapon->link_id) {
            if (removal->result == WeaponSimulateResult::PlayerExplosion) {
              CreateExplosion(*weapon);
              removed = true;
            }
            break;
          }
        }

        if (removed) {
          assert(weapon_count > 0);
          weapons[i--] = weapons[--weapon_count];
        }
      }
    }
  }
}

WeaponSimulateResult WeaponManager::Simulate(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  if (weapon.last_tick++ >= weapon.end_tick) return WeaponSimulateResult::TimedOut;

  if (type == WeaponType::Repel) {
    return SimulateRepel(weapon);
  }

  Vector2f previous_position = weapon.position;

  WeaponSimulateResult position_result = SimulatePosition(weapon);

  if (position_result != WeaponSimulateResult::Continue) {
    return position_result;
  }

  if (type == WeaponType::Decoy) return WeaponSimulateResult::Continue;

  bool is_bomb = weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb ||
                 weapon.data.type == WeaponType::Thor;

  bool is_prox = weapon.data.type == WeaponType::ProximityBomb || weapon.data.type == WeaponType::Thor;

  if (is_prox && weapon.prox_hit_player_id != 0xFFFF) {
    Player* hit_player = player_manager.GetPlayerById(weapon.prox_hit_player_id);

    if (!hit_player) {
      return WeaponSimulateResult::PlayerExplosion;
    }

    float dx = abs(weapon.position.x - hit_player->position.x);
    float dy = abs(weapon.position.y - hit_player->position.y);

    float highest = dx > dy ? dx : dy;

    if (highest > weapon.prox_highest_offset || GetCurrentTick() >= weapon.sensor_end_tick) {
      if (ship_controller) {
        ship_controller->OnWeaponHit(weapon);
      }

      weapon.position = previous_position;

      return WeaponSimulateResult::PlayerExplosion;
    } else {
      weapon.prox_highest_offset = highest;
    }

    return WeaponSimulateResult::Continue;
  }

  if (type == WeaponType::Burst && !(weapon.flags & WEAPON_FLAG_BURST_ACTIVE)) {
    return WeaponSimulateResult::Continue;
  }

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;
    if (player->frequency == weapon.frequency) continue;
    if (player->enter_delay > 0) continue;

    float radius = connection.settings.ShipSettings[player->ship].GetRadius();
    Vector2f player_r(radius, radius);
    Vector2f& pos = player->position;

    float weapon_radius = 18.0f;

    if (is_prox) {
      float prox = (float)(connection.settings.ProximityDistance + weapon.data.level);

      if (weapon.data.type == WeaponType::Thor) {
        prox += 3;
      }

      weapon_radius = prox * 18.0f;
    }

    weapon_radius = (weapon_radius - 14.0f) / 16.0f;

    Vector2f min_w(weapon.position.x - weapon_radius, weapon.position.y - weapon_radius);
    Vector2f max_w(weapon.position.x + weapon_radius, weapon.position.y + weapon_radius);

    if (BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w)) {
      bool hit = true;

      if (is_prox) {
        weapon.prox_hit_player_id = player->id;
        weapon.sensor_end_tick = GetCurrentTick() + connection.settings.BombExplodeDelay;

        float dx = abs(weapon.position.x - player->position.x);
        float dy = abs(weapon.position.y - player->position.y);

        if (dx > dy) {
          weapon.prox_highest_offset = dx;
        } else {
          weapon.prox_highest_offset = dy;
        }

        weapon_radius = 4.0f / 16.0f;

        min_w = Vector2f(weapon.position.x - weapon_radius, weapon.position.y - weapon_radius);
        max_w = Vector2f(weapon.position.x + weapon_radius, weapon.position.y + weapon_radius);

        // Fully trigger the bomb if it hits the player's normal radius check
        hit = BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w);

        if (!hit) {
          return WeaponSimulateResult::Continue;
        }
      }

      if (hit && (is_bomb || player->id == player_manager.player_id) && !HasLinkRemoved(weapon.link_id)) {
        if (ship_controller) {
          ship_controller->OnWeaponHit(weapon);
        }
      }

      // Move the position back so shrap spawns correctly
      if (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
        weapon.position = previous_position;
      }

      return WeaponSimulateResult::PlayerExplosion;
    }
  }

  return WeaponSimulateResult::Continue;
}

WeaponSimulateResult WeaponManager::SimulateRepel(Weapon& weapon) {
  float effect_radius = connection.settings.RepelDistance / 16.0f;
  float effect_radius_sq = effect_radius * effect_radius;
  float speed = connection.settings.RepelSpeed / 16.0f / 10.0f;

  u64 time = GetTime();

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon& other = weapons[i];

    if (other.frequency == weapon.frequency) continue;
    if (other.data.type == WeaponType::Repel) continue;

    float dist_sq = other.position.DistanceSq(weapon.position);

    if (dist_sq <= effect_radius_sq) {
      Vector2f direction = Normalize(other.position - weapon.position);

      other.velocity = direction * speed;
      other.last_event_time = time;
      other.last_event_position = other.position;

      WeaponType type = other.data.type;

      if (other.data.alternate && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
        other.data.alternate = 0;

        Player* player = player_manager.GetPlayerById(other.player_id);

        if (player) {
          SetWeaponSprite(*player, other);
        }
      }

      // Set end tick after turning mines into bombs so the new end tick is bomb time
      other.end_tick = GetCurrentTick() + GetWeaponTotalAliveTime(other.data.type, other.data.alternate);
    }
  }

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player& player = player_manager.players[i];

    if (player.frequency == weapon.frequency) continue;
    if (player.ship >= 8) continue;
    if (player.enter_delay > 0.0f) continue;

    float dist_sq = player.position.DistanceSq(weapon.position);

    if (dist_sq <= effect_radius_sq) {
      if (connection.map.GetTileId(player.position) != kTileSafeId) {
        Vector2f direction = Normalize(player.position - weapon.position);

        player.velocity = direction * speed;
        player.lerp_time = 0.0f;
      }
    }
  }

  return WeaponSimulateResult::Continue;
}

bool WeaponManager::SimulateAxis(Weapon& weapon, float dt, int axis) {
  float previous = weapon.position[axis];
  Map& map = connection.map;

  weapon.position[axis] += weapon.velocity[axis] * dt;

  if (weapon.data.type == WeaponType::Thor) return false;

  // TODO: Handle other special tiles here
  if (map.IsSolid((u16)std::floor(weapon.position.x), (u16)std::floor(weapon.position.y))) {
    weapon.position[axis] = previous;
    weapon.velocity[axis] = -weapon.velocity[axis];

    return true;
  }

  return false;
}

WeaponSimulateResult WeaponManager::SimulatePosition(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  // This collision method deviates from Continuum when using variable update rate, so it updates by one tick at a time
  bool x_collide = SimulateAxis(weapon, 1.0f / 100.0f, 0);
  bool y_collide = SimulateAxis(weapon, 1.0f / 100.0f, 1);

  if (x_collide || y_collide) {
    weapon.last_event_time = GetTime();
    weapon.last_event_position = weapon.position;

    if ((type == WeaponType::Bullet || type == WeaponType::BouncingBullet) && weapon.data.shrap > 0) {
      s32 remaining = weapon.end_tick - GetCurrentTick();
      s32 duration = connection.settings.BulletAliveTime - remaining;

      if (remaining < 0 || duration <= 25) {
        return WeaponSimulateResult::TimedOut;
      }
    }

    if (type == WeaponType::Bullet || type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
      if (weapon.bounces_remaining == 0) {
        if ((type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && ship_controller) {
          ship_controller->OnWeaponHit(weapon);
        }

        return WeaponSimulateResult::WallExplosion;
      }

      if (--weapon.bounces_remaining == 0 && !(weapon.flags & WEAPON_FLAG_EMP)) {
        weapon.animation.sprite = Graphics::anim_bombs + weapon.data.level;
      }
    } else if (type == WeaponType::Burst) {
      weapon.flags |= WEAPON_FLAG_BURST_ACTIVE;
      weapon.animation.sprite = &Graphics::anim_burst_active;
    }
  }

  return WeaponSimulateResult::Continue;
}

void WeaponManager::ClearWeapons(Player& player) {
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->player_id == player.id) {
      weapons[i--] = weapons[--weapon_count];
    }
  }
}

bool WeaponManager::HasLinkRemoved(u32 link_id) {
  for (size_t i = 0; i < link_removal_count; ++i) {
    if (link_removals[i].link_id == link_id) {
      return true;
    }
  }

  return false;
}

void WeaponManager::AddLinkRemoval(u32 link_id, WeaponSimulateResult result) {
  assert(link_removal_count < NULLSPACE_ARRAY_SIZE(link_removals));

  WeaponLinkRemoval* removal = link_removals + link_removal_count++;
  removal->link_id = link_id;
  removal->result = result;
}

void WeaponManager::CreateExplosion(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      if (weapon.flags & WEAPON_FLAG_EMP) {
        Vector2f offset = Graphics::anim_emp_explode.frames[0].dimensions * (0.5f / 16.0f);
        animation.AddAnimation(Graphics::anim_emp_explode, weapon.position - offset)->layer = Layer::Explosions;

        PlayPositionalSound(AudioType::EBombExplode, weapon.position);
      } else {
        Vector2f offset = Graphics::anim_bomb_explode.frames[0].dimensions * (0.5f / 16.0f);
        animation.AddAnimation(Graphics::anim_bomb_explode, weapon.position - offset)->layer = Layer::Explosions;

        PlayPositionalSound(AudioType::Explode2, weapon.position);
      }

      constexpr u32 kExplosionIndicatorTicks = 125;
      radar->AddTemporaryIndicator(weapon.position, GetCurrentTick() + kExplosionIndicatorTicks, Vector2f(2, 2),
                                   ColorType::RadarExplosion);

      s32 count = weapon.data.shrap;

      VieRNG rng = {(s32)weapon.rng_seed};

      for (s32 i = 0; i < count; ++i) {
        s32 orientation = 0;

        if (!connection.settings.ShrapnelRandom) {
          orientation = (i * 40000) / count * 9;
        } else {
          orientation = (rng.GetNext() % 40000) * 9;
        }

        Vector2f direction(sin(Radians(orientation / 1000.0f)), -cos(Radians(orientation / 1000.0f)));

        float speed = connection.settings.ShrapnelSpeed / 10.0f / 16.0f;

        Weapon* shrap = weapons + weapon_count++;

        shrap->animation.t = 0.0f;
        shrap->animation.repeat = true;
        shrap->bounces_remaining = 0;
        shrap->data = weapon.data;
        shrap->data.level = weapon.data.shraplevel;
        if (weapon.data.shrapbouncing) {
          shrap->data.type = WeaponType::BouncingBullet;
          shrap->animation.sprite = Graphics::anim_bounce_shrapnel + weapon.data.shraplevel;
        } else {
          shrap->data.type = WeaponType::Bullet;
          shrap->animation.sprite = Graphics::anim_shrapnel + weapon.data.shraplevel;
        }
        shrap->flags = 0;
        shrap->frequency = weapon.frequency;
        shrap->link_id = 0xFFFFFFFF;
        shrap->player_id = weapon.player_id;
        shrap->velocity = Normalize(direction) * speed;
        shrap->position = weapon.position;
        shrap->last_tick = GetCurrentTick();
        shrap->end_tick = shrap->last_tick + connection.settings.BulletAliveTime;
        shrap->last_event_position = shrap->position;
        shrap->last_event_time = GetTime();

        if (connection.map.IsSolid((u16)shrap->position.x, (u16)shrap->position.y)) {
          --weapon_count;
        }
      }

      weapon.rng_seed = (u32)rng.seed;
    } break;
    case WeaponType::BouncingBullet:
    case WeaponType::Bullet: {
      Vector2f offset = Graphics::anim_bullet_explode.frames[0].dimensions * (0.5f / 16.0f);
      // Render the tiny explosions below the bomb explosions so they don't look weird
      animation.AddAnimation(Graphics::anim_bullet_explode, weapon.position - offset)->layer = Layer::AfterShips;
    } break;
    default: {
    } break;
  }
}

void WeaponManager::Render(Camera& camera, Camera& ui_camera, SpriteRenderer& renderer, float dt,
                           const RadarVisibility& radar_visibility) {
#pragma pack(push, 1)
  struct DecoyRenderRequest {
    Player* player;
    Vector2f position;
  };
#pragma pack(pop)

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  // WARNING: temp_arena must not be used for anything else here or in any calls made here.
  // All of the decoys are allocated contiguously so the base decoys array can access all of them
  DecoyRenderRequest* decoys = (DecoyRenderRequest*)temp_arena.Allocate(0);
  size_t decoy_count = 0;

  Player* self = player_manager.GetSelf();

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->animation.sprite) {
      weapon->animation.t += dt;

      if (!weapon->animation.IsAnimating() && weapon->animation.repeat) {
        weapon->animation.t -= weapon->animation.sprite->duration;
      }
    }

    u32 tick = GetCurrentTick();
    u64 time = GetTime();
    float elapsed_seconds = (time - weapon->last_event_time) / 1000000.0f;
    Vector2f travel_ray = elapsed_seconds * weapon->velocity;
    Vector2f extrapolated_pos;

    u16 see_bomb_level = radar_visibility.see_bomb_level;

    if (weapon->data.type == WeaponType::Bomb || weapon->data.type == WeaponType::ProximityBomb) {
      bool add_indicator = false;

      if (!weapon->data.alternate && see_bomb_level > 0 && (weapon->data.level + 1) >= see_bomb_level) {
        add_indicator = true;
      } else if (weapon->data.alternate && radar_visibility.see_mines) {
        add_indicator = true;
      }

      if (add_indicator) {
        radar->AddTemporaryIndicator(weapon->position, 0, Vector2f(2, 2), ColorType::RadarBomb);
      }
    }

    if (weapon->data.type != WeaponType::Thor) {
      // TODO: This is pretty heavy. Maybe make it an option to toggle off and just use the simulated position
      CastResult cast = connection.map.Cast(weapon->last_event_position, Normalize(travel_ray), travel_ray.Length());
      extrapolated_pos = cast.position;

      float desync_threshold = weapon->velocity.Length() * (4.0f / 100.0f);

      // Resync render position when deviating too much from projected path
      if (extrapolated_pos.DistanceSq(weapon->position) > desync_threshold * desync_threshold) {
        extrapolated_pos = weapon->position;
        weapon->last_event_position = weapon->position;
        weapon->last_event_time = time;
      }
    } else {
      extrapolated_pos = weapon->last_event_position + travel_ray;
    }

    if (weapon->animation.IsAnimating()) {
      SpriteRenderable& frame = weapon->animation.GetFrame();
      Vector2f position = extrapolated_pos - frame.dimensions * (0.5f / 16.0f);

      renderer.Draw(camera, frame, position.PixelRounded(), Layer::Weapons);
    } else if (weapon->data.type == WeaponType::Decoy) {
      Player* player = player_manager.GetPlayerById(weapon->player_id);

      if (player) {
        float orientation = weapon->initial_orientation - (player->orientation - weapon->initial_orientation);

        if (orientation < 0.0f) {
          orientation += 1.0f;
        } else if (orientation >= 1.0f) {
          orientation -= 1.0f;
        }

        u8 direction = (u8)(orientation * 40);
        assert(direction < 40);

        size_t index = player->ship * 40 + direction;
        SpriteRenderable& frame = Graphics::ship_sprites[index];
        Vector2f position = extrapolated_pos - frame.dimensions * (0.5f / 16.0f);

        renderer.Draw(camera, frame, position.PixelRounded(), Layer::Ships);

        if (self && player->id != self->id) {
          player_manager.RenderPlayerName(camera, renderer, *self, *player, extrapolated_pos, true);
        }

        // Push them on here to minimize draw calls
        DecoyRenderRequest* request = memory_arena_push_type(&temp_arena, DecoyRenderRequest);
        request->player = player;
        request->position = weapon->position;

        ++decoy_count;
      }
    }
  }

  renderer.Render(camera);

  if (self && radar) {
    for (size_t i = 0; i < decoy_count; ++i) {
      DecoyRenderRequest* request = decoys + i;

      radar->RenderDecoy(ui_camera, renderer, *self, *request->player, request->position);
    }

    renderer.Render(ui_camera);
  }

  temp_arena.Revert(snapshot);
}

u32 WeaponManager::CalculateRngSeed(u32 x, u32 y, u32 vel_x, u32 vel_y, u16 shrap_count, u16 weapon_level,
                                    u32 frequency) {
  u32 x1000 = (u32)(x * 1000);
  u32 y1000 = (u32)(y * 1000);
  u32 x_vel = (u32)(vel_x);
  u32 y_vel = (u32)(vel_y);

  return shrap_count + weapon_level + x1000 + y1000 + x_vel + y_vel + frequency;
}

void WeaponManager::OnWeaponPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 direction = buffer.ReadU8();
  u16 timestamp = buffer.ReadU16();
  u16 x = buffer.ReadU16();
  s16 vel_y = (s16)buffer.ReadU16();
  u16 pid = buffer.ReadU16();
  s16 vel_x = (s16)buffer.ReadU16();
  u8 checksum = buffer.ReadU8();
  buffer.ReadU8();  // Togglables
  u8 ping = buffer.ReadU8();
  u16 y = buffer.ReadU16();
  buffer.ReadU16();  // Bounty
  u16 weapon_data = buffer.ReadU16();

  if (weapon_data == 0) return;

  // Player sends out position packet with their timestamp, it takes ping ticks to reach server, server re-timestamps it
  // and sends it to us.
  u32 server_tick = GetCurrentTick() + connection.time_diff;
  u32 server_timestamp = ((server_tick & 0xFFFF0000) | timestamp);
  u32 local_timestamp = server_timestamp - connection.time_diff - ping;

  Player* player = player_manager.GetPlayerById(pid);
  if (!player) return;

  Vector2f position(x / 16.0f, y / 16.0f);
  Vector2f velocity(vel_x / 16.0f / 10.0f, vel_y / 16.0f / 10.0f);
  WeaponData data = *(WeaponData*)&weapon_data;

  FireWeapons(*player, data, x, y, vel_x, vel_y, local_timestamp);
}

bool WeaponManager::FireWeapons(Player& player, WeaponData weapon, u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y,
                                u32 timestamp) {
  ShipSettings& ship_settings = connection.settings.ShipSettings[player.ship];
  WeaponType type = weapon.type;

  u8 direction = (u8)(player.orientation * 40.0f);
  u16 pid = player.id;

  if ((type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon.alternate) {
    Player* self = player_manager.GetSelf();

    if (self && self->id == player.id) {
      size_t self_count = 0;
      size_t team_count = 0;

      GetMineCounts(player, &self_count, &team_count);

      if (self_count >= ship_settings.MaxMines) {
        player_manager.notifications->PushFormatted(TextColor::Yellow, "You have too many mines");
        return false;
      }

      if (team_count >= (size_t)connection.settings.TeamMaxMines) {
        player_manager.notifications->PushFormatted(TextColor::Yellow, "Team has too many mines");
        return false;
      }
    }
  }

  if (g_Settings.sound_enabled) {
    AudioType audio_type = GetAudioType(ship_settings, weapon);

    PlayPositionalSound(audio_type, player.position);
  }

  if (type == WeaponType::Bullet || type == WeaponType::BouncingBullet) {
    bool dbarrel = ship_settings.DoubleBarrel;

    Vector2f heading = OrientationToHeading(direction);

    u32 link_id = next_link_id++;

    WeaponSimulateResult result;
    bool destroy_link = false;

    if (dbarrel) {
      Vector2f perp = Perpendicular(heading);
      Vector2f offset = perp * (ship_settings.GetRadius() * 0.75f);

      result = GenerateWeapon(pid, weapon, timestamp, pos_x - (s32)(offset.x * 16.0f), pos_y - (s32)(offset.y * 16.0f),
                              vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }

      result = GenerateWeapon(pid, weapon, timestamp, pos_x + (s32)(offset.x * 16.0f), pos_y + (s32)(offset.y * 16.0f),
                              vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    } else {
      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    }

    if (weapon.alternate) {
      float rads = Radians(ship_settings.MultiFireAngle / 111.0f);
      Vector2f first_heading = Rotate(heading, rads);
      Vector2f second_heading = Rotate(heading, -rads);

      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, first_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, second_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    }

    if (destroy_link) {
      for (size_t i = 0; i < weapon_count; ++i) {
        Weapon* weapon = weapons + i;
        if (weapon->link_id == link_id) {
          CreateExplosion(*weapon);
          weapons[i--] = weapons[--weapon_count];
        }
      }
    }
  } else if (type == WeaponType::Burst) {
    u8 count = connection.settings.ShipSettings[player.ship].BurstShrapnel;

    for (s32 i = 0; i < count; ++i) {
      s32 orientation = (i * 40000) / count * 9;
      Vector2f direction(sin(Radians(orientation / 1000.0f)), -cos(Radians(orientation / 1000.0f)));

      GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, 0, 0, direction, kInvalidLink);
    }
  } else {
    GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, OrientationToHeading(direction), kInvalidLink);
  }

  return true;
}

WeaponSimulateResult WeaponManager::GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp,
                                                   u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y, const Vector2f& heading,
                                                   u32 link_id) {
  Weapon* weapon = weapons + weapon_count++;

  weapon->data = weapon_data;
  weapon->player_id = player_id;
  weapon->position = Vector2f(pos_x / 16.0f, pos_y / 16.0f);
  weapon->bounces_remaining = 0;
  weapon->flags = 0;
  weapon->link_id = link_id;
  weapon->prox_hit_player_id = 0xFFFF;
  weapon->last_tick = local_timestamp;

  WeaponType type = weapon->data.type;

  Player* player = player_manager.GetPlayerById(player_id);
  assert(player);

  weapon->frequency = player->frequency;

  s16 speed = 0;
  switch (type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      speed = (s16)connection.settings.ShipSettings[player->ship].BulletSpeed;
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (!weapon->data.alternate) {
        speed = (s16)connection.settings.ShipSettings[player->ship].BombSpeed;
        weapon->bounces_remaining = connection.settings.ShipSettings[player->ship].BombBounceCount;
      }
    } break;
    case WeaponType::Decoy: {
      weapon->initial_orientation = player->orientation;
    } break;
    case WeaponType::Burst: {
      speed = (s16)connection.settings.ShipSettings[player->ship].BurstSpeed;
    } break;
    default: {
    } break;
  }

  weapon->end_tick = local_timestamp + GetWeaponTotalAliveTime(weapon->data.type, weapon->data.alternate);

  bool is_mine = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon->data.alternate;

  if (type != WeaponType::Repel && !is_mine) {
    weapon->velocity = Vector2f(vel_x / 16.0f / 10.0f, vel_y / 16.0f / 10.0f) + heading * (speed / 16.0f / 10.0f);
  } else {
    weapon->velocity = Vector2f(0, 0);
  }

  s32 tick_diff = TICK_DIFF(GetCurrentTick(), local_timestamp);

  WeaponSimulateResult result = WeaponSimulateResult::Continue;

  for (s32 i = 0; i < tick_diff; ++i) {
    result = Simulate(*weapon);

    if (result != WeaponSimulateResult::Continue) {
      if (type == WeaponType::Repel) {
        // Create an animation even if the repel was instant.
        Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

        Animation* anim =
            animation.AddAnimation(Graphics::anim_repel, weapon->position.PixelRounded() - offset.PixelRounded());
        anim->layer = Layer::AfterShips;
        anim->repeat = false;
      }

      CreateExplosion(*weapon);
      --weapon_count;
      return result;
    }
  }

  weapon->last_event_position = weapon->position;
  weapon->last_event_time = GetTime();

  vel_x = (s32)(weapon->velocity.x * 16.0f * 10.0f);
  vel_y = (s32)(weapon->velocity.y * 16.0f * 10.0f);

  weapon->rng_seed =
      CalculateRngSeed(pos_x, pos_y, vel_x, vel_y, weapon_data.shrap, weapon_data.level, player->frequency);

  weapon->animation.t = 0.0f;
  weapon->animation.sprite = nullptr;
  weapon->animation.repeat = true;
  weapon->last_trail_tick = 0;

  SetWeaponSprite(*player, *weapon);

  if (player->id == player_manager.player_id &&
      (type == WeaponType::Bomb || type == WeaponType::ProximityBomb || type == WeaponType::Thor) &&
      !weapon->data.alternate) {
    player->bombflash_anim_t = 0.0f;
  }

  return result;
}

u64 WeaponManager::GetTime() {
  return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now())
      .time_since_epoch()
      .count();
}

void WeaponManager::SetWeaponSprite(Player& player, Weapon& weapon) {
  WeaponType type = weapon.data.type;

  switch (type) {
    case WeaponType::Bullet: {
      weapon.animation.sprite = Graphics::anim_bullets + weapon.data.level;
    } break;
    case WeaponType::BouncingBullet: {
      weapon.animation.sprite = Graphics::anim_bullets_bounce + weapon.data.level;
    } break;
    case WeaponType::ProximityBomb:
    case WeaponType::Bomb: {
      bool emp = connection.settings.ShipSettings[player.ship].EmpBomb;

      if (weapon.data.alternate) {
        if (emp) {
          weapon.animation.sprite = Graphics::anim_emp_mines + weapon.data.level;
          weapon.flags |= WEAPON_FLAG_EMP;
        } else {
          weapon.animation.sprite = Graphics::anim_mines + weapon.data.level;
        }
      } else {
        if (emp) {
          weapon.animation.sprite = Graphics::anim_emp_bombs + weapon.data.level;
          weapon.flags |= WEAPON_FLAG_EMP;
        } else {
          if (weapon.bounces_remaining > 0) {
            weapon.animation.sprite = Graphics::anim_bombs_bounceable + weapon.data.level;
          } else {
            weapon.animation.sprite = Graphics::anim_bombs + weapon.data.level;
          }
        }
      }
    } break;
    case WeaponType::Thor: {
      weapon.animation.sprite = &Graphics::anim_thor;
    } break;
    case WeaponType::Repel: {
      Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

      Animation* anim =
          animation.AddAnimation(Graphics::anim_repel, weapon.position.PixelRounded() - offset.PixelRounded());
      anim->layer = Layer::AfterShips;
      anim->repeat = false;

      weapon.animation.sprite = nullptr;
      weapon.animation.repeat = false;
    } break;
    case WeaponType::Burst: {
      weapon.animation.sprite = &Graphics::anim_burst_inactive;
    } break;
    default: {
    } break;
  }

  weapon.animation.t = 0.0f;
}

int WeaponManager::GetWeaponTotalAliveTime(WeaponType type, bool alternate) {
  int result = 0;

  switch (type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      result = connection.settings.BulletAliveTime;
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (alternate) {
        result = connection.settings.MineAliveTime;
      } else {
        result = connection.settings.BombAliveTime;
      }
    } break;
    case WeaponType::Repel: {
      result = connection.settings.RepelTime;
    } break;
    case WeaponType::Decoy: {
      result = connection.settings.DecoyAliveTime;
    } break;
    case WeaponType::Burst: {
      result = connection.settings.BulletAliveTime;
    } break;
    default: {
    } break;
  }

  return result;
}

void WeaponManager::PlayPositionalSound(AudioType type, const Vector2f& position) {
  Player* self = player_manager.GetSelf();

  if (self) {
    Vector2f view_half_extent_tiles = connection.view_dim * (0.5f / 16.0f);
    float sound_radius = view_half_extent_tiles.x;

    if (view_half_extent_tiles.y > sound_radius) {
      sound_radius = view_half_extent_tiles.y;
    }

    sound_radius += g_Settings.sound_radius_increase;

    if (self->position.DistanceSq(position) <= sound_radius * sound_radius) {
      float distance = self->position.Distance(position);
      float volume = 1.0f - (distance / sound_radius);

      if (volume < 0.0f) {
        volume = 0.0f;
      }

      sound_system.Play(type, volume);
    }
  }
}

void WeaponManager::GetMineCounts(Player& player, size_t* player_count, size_t* team_count) {
  *player_count = 0;
  *team_count = 0;

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;
    WeaponType type = weapon->data.type;

    if (weapon->data.alternate && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
      if (weapon->player_id == player.id) {
        (*player_count)++;
      }

      if (weapon->frequency == player.frequency) {
        (*team_count)++;
      }
    }
  }
}

}  // namespace null
