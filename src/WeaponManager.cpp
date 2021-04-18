#include "WeaponManager.h"

#include <cassert>
#include <cmath>

#include "Buffer.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

static Vector2f GetHeading(u8 discrete_rotation) {
  const float kToRads = (3.14159f / 180.0f);
  float rads = (((40 - (discrete_rotation + 30)) % 40) * 9.0f) * kToRads;
  float x = cos(rads);
  float y = -sin(rads);

  return Vector2f(x, y);
}

static void OnLargePositionPkt(void* user, u8* pkt, size_t size) {
  WeaponManager* manager = (WeaponManager*)user;

  manager->OnWeaponPacket(pkt, size);
}

WeaponManager::WeaponManager(Connection& connection, PlayerManager& player_manager, PacketDispatcher& dispatcher,
                             AnimationSystem& animation)
    : connection(connection), player_manager(player_manager), animation(animation) {
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
}

void WeaponManager::Update(float dt) {
  u32 tick = GetCurrentTick();

  // TODO: Remove if player enters safe
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->end_tick <= tick) {
      // Swap with last weapon and process it next
      weapons[i--] = weapons[--weapon_count];
      continue;
    }

    weapon->position += weapon->velocity * dt;

    if (weapon->animation.sprite) {
      weapon->animation.t += dt;

      if (!weapon->animation.IsAnimating() && weapon->animation.repeat) {
        weapon->animation.t -= weapon->animation.sprite->duration;
      }

      if (weapon->data.type == (u16)WeaponType::Bullet || weapon->data.type == (u16)WeaponType::BouncingBullet) {
        if (TICK_DIFF(tick, weapon->last_trail_tick) >= 1) {
          SpriteRenderable& frame = Graphics::anim_bullet_trails[weapon->data.level].frames[0];
          Vector2f position = weapon->position - frame.dimensions * (0.5f / 16.0f);

          u32 pos_x = (u32)(position.x * 16.0f);
          u32 pos_y = (u32)(position.y * 16.0f);
          position = Vector2f(pos_x / 16.0f, pos_y / 16.0f);

          animation.AddAnimation(Graphics::anim_bullet_trails[weapon->data.level], position);
          weapon->last_trail_tick = tick;
        }
      } else if ((weapon->data.type == (u16)WeaponType::Bomb || weapon->data.type == (u16)WeaponType::ProximityBomb) &&
                 !weapon->data.alternate) {
        if (TICK_DIFF(tick, weapon->last_trail_tick) >= 5) {
          SpriteRenderable& frame = Graphics::anim_bomb_trails[weapon->data.level].frames[0];
          Vector2f position = weapon->position - frame.dimensions * (0.5f / 16.0f);

          u32 pos_x = (u32)(position.x * 16.0f);
          u32 pos_y = (u32)(position.y * 16.0f);
          position = Vector2f(pos_x / 16.0f, pos_y / 16.0f);

          animation.AddAnimation(Graphics::anim_bomb_trails[weapon->data.level], position);
          weapon->last_trail_tick = tick;
        }
      }
    }
  }
}

void WeaponManager::Render(Camera& camera, SpriteRenderer& renderer) {
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->animation.IsAnimating()) {
      SpriteRenderable& frame = weapon->animation.GetFrame();
      Vector2f position = weapon->position - frame.dimensions * (0.5f / 16.0f);

      u32 pos_x = (u32)(position.x * 16.0f);
      u32 pos_y = (u32)(position.y * 16.0f);
      position = Vector2f(pos_x / 16.0f, pos_y / 16.0f);

      renderer.Draw(camera, frame, position);
    } else if (weapon->data.type == (u16)WeaponType::Decoy) {
      Player* player = player_manager.GetPlayerById(weapon->player_id);
      if (player) {
        // TODO: Render opposite rotation based on initial orientation
        size_t index = player->ship * 40 + player->direction;
        SpriteRenderable& frame = Graphics::ship_sprites[index];
        Vector2f position = weapon->position - frame.dimensions * (0.5f / 16.0f);

        renderer.Draw(camera, frame, position);
      }
    }
  }
}

void WeaponManager::OnWeaponPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 direction = buffer.ReadU8();
  u16 timestamp = buffer.ReadU16();
  u16 x = buffer.ReadU16();
  s16 vel_y = (s16)buffer.ReadU16();
  u16 pid = buffer.ReadU16();

  Vector2f velocity((s16)buffer.ReadU16() / 16.0f / 10.0f, vel_y / 16.0f / 10.0f);
  u8 checksum = buffer.ReadU8();
  buffer.ReadU8();  // Togglables
  u8 ping = buffer.ReadU8();
  u16 y = buffer.ReadU16();
  buffer.ReadU16();  // Bounty
  u16 weapon_data = buffer.ReadU16();

  if (weapon_data == 0) return;

  // Player sends out position packet with their timestamp, it takes ping ticks to reach server, server re-timestamps it
  // and sends it to us.
  // TODO: Should it be modified by ping to more closely match the sender's weapon timeout tick?
  u32 server_tick = GetCurrentTick() + connection.time_diff;
  u32 server_timestamp = ((server_tick & 0xFFFF0000) | timestamp);
  u32 local_timestamp = server_timestamp - connection.time_diff - ping;

  WeaponData data = *(WeaponData*)&weapon_data;
  WeaponType type = (WeaponType)data.type;

  Player* player = player_manager.GetPlayerById(pid);
  if (!player) return;

  Vector2f position(x / 16.0f, y / 16.0f);

  ShipSettings& ship_settings = connection.settings.ShipSettings[player->ship];

  if (type == WeaponType::Bullet || type == WeaponType::BouncingBullet) {
    bool dbarrel = ship_settings.DoubleBarrel;

    Vector2f heading = GetHeading(direction);

    // TODO: Generate linked

    if (dbarrel) {
      Vector2f perp = Perpendicular(heading);
      Vector2f offset = perp * (ship_settings.GetRadius() * 0.75f);

      GenerateWeapon(pid, data, local_timestamp, position - offset, velocity, heading, kInvalidLink);
      GenerateWeapon(pid, data, local_timestamp, position + offset, velocity, heading, kInvalidLink);
    } else {
      GenerateWeapon(pid, data, local_timestamp, position, velocity, heading, kInvalidLink);
    }

    if (data.alternate) {
      float rads = Radians(ship_settings.MultiFireAngle / 111.0f);
      Vector2f first_heading = Rotate(heading, rads);
      Vector2f second_heading = Rotate(heading, -rads);

      GenerateWeapon(pid, data, local_timestamp, position, velocity, first_heading, kInvalidLink);
      GenerateWeapon(pid, data, local_timestamp, position, velocity, second_heading, kInvalidLink);
    }
  } else {
    GenerateWeapon(pid, data, local_timestamp, position, velocity, GetHeading(direction), kInvalidLink);
  }
}

void WeaponManager::GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp, const Vector2f& position,
                                   const Vector2f& velocity, const Vector2f& heading, u32 link_id) {
  Weapon* weapon = weapons + weapon_count++;

  weapon->data = weapon_data;
  weapon->player_id = player_id;
  weapon->position = position;

  WeaponType type = (WeaponType)weapon->data.type;

  Player* player = player_manager.GetPlayerById(player_id);
  assert(player);

  u16 speed = 0;
  switch (type) {
    case WeaponType::Burst:
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      weapon->end_tick = local_timestamp + connection.settings.BulletAliveTime;
      speed = connection.settings.ShipSettings[player->ship].BulletSpeed;
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (weapon->data.alternate) {
        weapon->end_tick = local_timestamp + connection.settings.MineAliveTime;
      } else {
        weapon->end_tick = local_timestamp + connection.settings.BombAliveTime;
        speed = connection.settings.ShipSettings[player->ship].BombSpeed;
      }
    } break;
    case WeaponType::Repel: {
      weapon->end_tick = local_timestamp + connection.settings.RepelTime;
    } break;
    case WeaponType::Decoy: {
      weapon->end_tick = local_timestamp + connection.settings.DecoyAliveTime;
    } break;
    default: {
    } break;
  }

  bool is_mine = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon->data.alternate;

  if (type != WeaponType::Repel && !is_mine) {
    weapon->velocity = velocity + heading * (speed / 16.0f / 10.0f);
  } else {
    weapon->velocity = Vector2f(0, 0);
  }

  s32 tick_diff = TICK_DIFF(GetCurrentTick(), local_timestamp);

  // TODO: Simulate forward
  weapon->position += weapon->velocity * (tick_diff / 100.0f);

  weapon->link_id = 0xFFFFFFFF;

  weapon->animation.t = 0.0f;
  weapon->animation.sprite = nullptr;
  weapon->animation.repeat = true;
  weapon->last_trail_tick = 0;

  switch (type) {
    case WeaponType::Bullet: {
      weapon->animation.sprite = Graphics::anim_bullets + weapon->data.level;
    } break;
    case WeaponType::BouncingBullet: {
      weapon->animation.sprite = Graphics::anim_bullets_bounce + weapon->data.level;
    } break;
    case WeaponType::ProximityBomb:
    case WeaponType::Bomb: {
      bool emp = connection.settings.ShipSettings[player->ship].EmpBomb;

      if (weapon->data.alternate) {
        if (emp) {
          weapon->animation.sprite = Graphics::anim_emp_mines + weapon->data.level;
        } else {
          weapon->animation.sprite = Graphics::anim_mines + weapon->data.level;
        }
      } else {
        if (emp) {
          weapon->animation.sprite = Graphics::anim_emp_bombs + weapon->data.level;
        } else {
          weapon->animation.sprite = Graphics::anim_bombs + weapon->data.level;
        }
      }
    } break;
    case WeaponType::Thor: {
      weapon->animation.sprite = &Graphics::anim_thor;
    } break;
    case WeaponType::Repel: {
      weapon->animation.sprite = &Graphics::anim_repel;
      weapon->animation.repeat = false;
    } break;
    default: {
    } break;
  }
}

}  // namespace null
