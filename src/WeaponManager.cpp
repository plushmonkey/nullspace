#include "WeaponManager.h"

#include <cassert>
#include <cmath>

#include "Buffer.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"

namespace null {

constexpr s32 kTrailDelayTicks = 3;

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

void WeaponManager::Initialize(SpriteRenderer& renderer) {
  int count = 0;

  bullet_renderables = renderer.LoadSheet("graphics/bullets.bm2", Vector2f(5, 5), &count);
  for (size_t i = 0; i < 4; ++i) {
    anim_bullets[i].frames = bullet_renderables + i * 4;
    anim_bullets[i].frame_count = 4;
    anim_bullets[i].duration = 0.1f;
  }

  for (size_t i = 0; i < 4; ++i) {
    anim_bullets_bounce[i].frames = bullet_renderables + i * 4 + 20;
    anim_bullets_bounce[i].frame_count = 4;
    anim_bullets_bounce[i].duration = 0.1f;
  }

  bullet_trail_renderables = renderer.LoadSheet("graphics/gradient.bm2", Vector2f(1, 1), &count);
  for (size_t i = 0; i < 3; ++i) {
    anim_bullet_trails[2 - i].frames = bullet_trail_renderables + i * 14;
    anim_bullet_trails[2 - i].frame_count = 7;
    anim_bullet_trails[2 - i].duration = 0.1f;
  }
  anim_bullet_trails[3] = anim_bullet_trails[2];
}

void WeaponManager::Update(float dt) {
  u32 tick = GetCurrentTick();

  bool drop_tail = false;

  if (TICK_DIFF(tick, last_trail_tick) >= kTrailDelayTicks) {
    drop_tail = true;
    last_trail_tick = tick;
  }

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

      if (!weapon->animation.IsAnimating()) {
        weapon->animation.t -= weapon->animation.sprite->duration;
      }

      if (drop_tail) {
        SpriteRenderable& frame = anim_bullet_trails[weapon->data.level].frames[0];
        Vector2f offset = frame.dimensions * (0.5f / 16.0f);

        animation.AddAnimation(anim_bullet_trails[weapon->data.level], weapon->position - offset);
      }
    }
  }
}

void WeaponManager::Render(Camera& camera, SpriteRenderer& renderer) {
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->animation.sprite == nullptr) continue;

    SpriteRenderable& frame = weapon->animation.GetFrame();
    Vector2f offset = frame.dimensions * (0.5f / 16.0f);

    renderer.Draw(camera, frame, weapon->position - offset);
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
  u32 local_timestamp = server_timestamp - connection.time_diff;

  WeaponData data = *(WeaponData*)&weapon_data;
  WeaponType type = (WeaponType)data.type;

  Player* player = player_manager.GetPlayerById(pid);
  assert(player);

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
    case WeaponType::Thor:  // TODO: I can't remember if thor has a timeout
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      weapon->end_tick = local_timestamp + connection.settings.BombAliveTime;
      speed = connection.settings.ShipSettings[player->ship].BombSpeed;
    } break;
    case WeaponType::Repel: {
      weapon->end_tick = local_timestamp + connection.settings.RepelTime;
    } break;
    case WeaponType::Decoy: {
      weapon->end_tick = local_timestamp + connection.settings.DecoyAliveTime;
    } break;
  }

  if (type != WeaponType::Repel) {
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

  switch (type) {
    case WeaponType::Bullet: {
      weapon->animation.sprite = this->anim_bullets + weapon->data.level;
    } break;
    case WeaponType::BouncingBullet: {
      weapon->animation.sprite = this->anim_bullets_bounce + weapon->data.level;
    } break;
  }
}

}  // namespace null
