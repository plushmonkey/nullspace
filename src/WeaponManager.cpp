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
  link_removal_count = 0;

  // TODO: Remove if player enters safe
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;
    WeaponSimulateResult result = Simulate(*weapon, tick, dt);

    if (result != WeaponSimulateResult::Continue && weapon->link_id != kInvalidLink) {
      assert(link_removal_count < NULLSPACE_ARRAY_SIZE(link_removals));

      WeaponLinkRemoval* removal = link_removals + link_removal_count++;
      removal->link_id = weapon->link_id;
      removal->result = result;
    }

    if (result == WeaponSimulateResult::PlayerExplosion || result == WeaponSimulateResult::WallExplosion) {
      CreateExplosion(*weapon);
      weapons[i--] = weapons[--weapon_count];
      continue;
    } else if (result == WeaponSimulateResult::TimedOut) {
      weapons[i--] = weapons[--weapon_count];
      continue;
    }

    if (weapon->animation.sprite) {
      weapon->animation.t += dt;

      if (!weapon->animation.IsAnimating() && weapon->animation.repeat) {
        weapon->animation.t -= weapon->animation.sprite->duration;
      }

      if (weapon->data.type == (u16)WeaponType::Bullet || weapon->data.type == (u16)WeaponType::BouncingBullet) {
        if (TICK_DIFF(tick, weapon->last_trail_tick) >= 1) {
          SpriteRenderable& frame = Graphics::anim_bullet_trails[weapon->data.level].frames[0];
          Vector2f offset = (frame.dimensions * (0.5f / 16.0f));
          Vector2f position = weapon->position.PixelRounded() - offset.PixelRounded();

          animation.AddAnimation(Graphics::anim_bullet_trails[weapon->data.level], position)->layer = Layer::AfterTiles;
          weapon->last_trail_tick = tick;
        }
      } else if ((weapon->data.type == (u16)WeaponType::Bomb || weapon->data.type == (u16)WeaponType::ProximityBomb) &&
                 !weapon->data.alternate) {
        if (TICK_DIFF(tick, weapon->last_trail_tick) >= 5) {
          SpriteRenderable& frame = Graphics::anim_bomb_trails[weapon->data.level].frames[0];
          Vector2f offset = (frame.dimensions * (0.5f / 16.0f));
          Vector2f position = weapon->position.PixelRounded() - offset.PixelRounded();

          animation.AddAnimation(Graphics::anim_bomb_trails[weapon->data.level], position)->layer = Layer::AfterTiles;
          weapon->last_trail_tick = tick;
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
          weapons[i--] = weapons[--weapon_count];
        }
      }
    }
  }
}

WeaponSimulateResult WeaponManager::Simulate(Weapon& weapon, u32 current_tick, float dt) {
  Map& map = connection.map;
  WeaponType type = (WeaponType)weapon.data.type;

  if (current_tick >= weapon.end_tick) return WeaponSimulateResult::TimedOut;

  // TODO: Implement velocity changes
  if (type == WeaponType::Repel) return WeaponSimulateResult::Continue;

  float dist = (weapon.velocity * dt).Length();
  for (int i = 0; i < 10 && dist > 0.0f; ++i) {
    CastResult result = map.Cast(weapon.position, Normalize(weapon.velocity), dist);

    if (!result.hit) {
      weapon.position = result.position;
      break;
    }

    if (type == WeaponType::Bullet || type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
      if (weapon.bounces_remaining == 0) {
        return WeaponSimulateResult::WallExplosion;
      }

      if (--weapon.bounces_remaining == 0 && !(weapon.flags & WEAPON_FLAG_EMP)) {
        weapon.animation.sprite = Graphics::anim_bombs + weapon.data.level;
      }
    }

    dist -= result.distance;
    weapon.velocity = weapon.velocity - 2.0f * (weapon.velocity.Dot(result.normal)) * result.normal;
  }

  if (type == WeaponType::Decoy) return WeaponSimulateResult::Continue;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;
    if (player->frequency == weapon.frequency) continue;
    if (player->explode_animation.IsAnimating()) continue;

    float radius = connection.settings.ShipSettings[player->ship].GetRadius();
    Vector2f r(radius, radius);
    Vector2f& pos = player->position;

    if (BoxContainsPoint(pos - r, pos + r, weapon.position)) {
      return WeaponSimulateResult::PlayerExplosion;
    }
  }

  return WeaponSimulateResult::Continue;
}

void WeaponManager::CreateExplosion(Weapon& weapon) {
  WeaponType type = (WeaponType)weapon.data.type;

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      if (weapon.flags & WEAPON_FLAG_EMP) {
        Vector2f offset = Graphics::anim_emp_explode.frames[0].dimensions * (0.5f / 16.0f);
        animation.AddAnimation(Graphics::anim_emp_explode, weapon.position - offset)->layer = Layer::Explosions;
      } else {
        Vector2f offset = Graphics::anim_bomb_explode.frames[0].dimensions * (0.5f / 16.0f);
        animation.AddAnimation(Graphics::anim_bomb_explode, weapon.position - offset)->layer = Layer::Explosions;
      }
    } break;
    case WeaponType::BouncingBullet:
    case WeaponType::Bullet: {
      Vector2f offset = Graphics::anim_bullet_explode.frames[0].dimensions * (0.5f / 16.0f);
      animation.AddAnimation(Graphics::anim_bullet_explode, weapon.position - offset)->layer = Layer::Explosions;
    } break;
    default: {
    } break;
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

      renderer.Draw(camera, frame, position, Layer::Weapons);
    } else if (weapon->data.type == (u16)WeaponType::Decoy) {
      Player* player = player_manager.GetPlayerById(weapon->player_id);
      if (player) {
        // TODO: Render opposite rotation based on initial orientation
        size_t index = player->ship * 40 + player->direction;
        SpriteRenderable& frame = Graphics::ship_sprites[index];
        Vector2f position = weapon->position - frame.dimensions * (0.5f / 16.0f);

        renderer.Draw(camera, frame, position, Layer::Ships);
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

    u32 link_id = next_link_id++;

    WeaponSimulateResult result;
    bool destroy_link = false;

    if (dbarrel) {
      Vector2f perp = Perpendicular(heading);
      Vector2f offset = perp * (ship_settings.GetRadius() * 0.75f);

      result = GenerateWeapon(pid, data, local_timestamp, position - offset, velocity, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        destroy_link = true;
      }

      result = GenerateWeapon(pid, data, local_timestamp, position + offset, velocity, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        destroy_link = true;
      }
    } else {
      result = GenerateWeapon(pid, data, local_timestamp, position, velocity, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        destroy_link = true;
      }
    }

    if (data.alternate) {
      float rads = Radians(ship_settings.MultiFireAngle / 111.0f);
      Vector2f first_heading = Rotate(heading, rads);
      Vector2f second_heading = Rotate(heading, -rads);

      result = GenerateWeapon(pid, data, local_timestamp, position, velocity, first_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        destroy_link = true;
      }
      result = GenerateWeapon(pid, data, local_timestamp, position, velocity, second_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
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

  } else {
    GenerateWeapon(pid, data, local_timestamp, position, velocity, GetHeading(direction), kInvalidLink);
  }
}

WeaponSimulateResult WeaponManager::GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp,
                                                   const Vector2f& position, const Vector2f& velocity,
                                                   const Vector2f& heading, u32 link_id) {
  Weapon* weapon = weapons + weapon_count++;

  weapon->data = weapon_data;
  weapon->player_id = player_id;
  weapon->position = position;
  weapon->bounces_remaining = 0;
  weapon->flags = 0;

  WeaponType type = (WeaponType)weapon->data.type;

  Player* player = player_manager.GetPlayerById(player_id);
  assert(player);

  weapon->frequency = player->frequency;

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
        weapon->bounces_remaining = connection.settings.ShipSettings[player->ship].BombBounceCount;
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

  WeaponSimulateResult result = WeaponSimulateResult::Continue;

  for (s32 i = 0; i < tick_diff; ++i) {
    result = Simulate(*weapon, GetCurrentTick(), 1.0f / 100.0f);
    if (result != WeaponSimulateResult::Continue) {
      if (type == WeaponType::Repel) {
        // Create an animation even if the repel was instant.
        Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

        Animation* anim = animation.AddAnimation(Graphics::anim_repel, position.PixelRounded() - offset.PixelRounded());
        anim->layer = Layer::AfterShips;
        anim->repeat = false;
      }

      CreateExplosion(*weapon);
      --weapon_count;
      return result;
    }
  }

  weapon->link_id = link_id;

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
          weapon->flags |= WEAPON_FLAG_EMP;
        } else {
          weapon->animation.sprite = Graphics::anim_mines + weapon->data.level;
        }
      } else {
        if (emp) {
          weapon->animation.sprite = Graphics::anim_emp_bombs + weapon->data.level;
          weapon->flags |= WEAPON_FLAG_EMP;
        } else {
          if (weapon->bounces_remaining > 0) {
            weapon->animation.sprite = Graphics::anim_bombs_bounceable + weapon->data.level;
          } else {
            weapon->animation.sprite = Graphics::anim_bombs + weapon->data.level;
          }
        }
      }
    } break;
    case WeaponType::Thor: {
      weapon->animation.sprite = &Graphics::anim_thor;
    } break;
    case WeaponType::Repel: {
      Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

      Animation* anim = animation.AddAnimation(Graphics::anim_repel, position.PixelRounded() - offset.PixelRounded());
      anim->layer = Layer::AfterShips;
      anim->repeat = false;

      weapon->animation.sprite = nullptr;
      weapon->animation.repeat = false;
    } break;
    default: {
    } break;
  }

  return result;
}

}  // namespace null
