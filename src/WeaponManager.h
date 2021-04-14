#ifndef NULLSPACE_WEAPONMANAGER_H_
#define NULLSPACE_WEAPONMANAGER_H_

#include "Player.h"
#include "Types.h"
#include "render/Animation.h"

namespace null {

enum class WeaponType { None, Bullet, BouncingBullet, Bomb, ProximityBomb, Repel, Decoy, Burst, Thor };
constexpr u32 kInvalidLink = 0xFFFFFFFF;

struct Weapon {
  Vector2f position;
  Vector2f velocity;

  u32 end_tick;

  u16 player_id;
  WeaponData data;

  // incremental id for connected multifire bullet
  u32 link_id = kInvalidLink;

  Animation animation;
};

struct Camera;
struct Connection;
struct PacketDispatcher;
struct PlayerManager;
struct SpriteRenderable;
struct SpriteRenderer;

struct WeaponManager {
  Connection& connection;
  PlayerManager& player_manager;
  AnimationSystem& animation;
  u32 next_link_id = 0;
  size_t weapon_count = 0;
  Weapon weapons[65535] = {};
  u32 last_trail_tick = 0;

  SpriteRenderable* bullet_renderables = nullptr;
  SpriteRenderable* bullet_trail_renderables = nullptr;
  AnimatedSprite anim_bullets[4] = {};
  AnimatedSprite anim_bullets_bounce[4] = {};
  AnimatedSprite anim_bullet_trails[4] = {};

  WeaponManager(Connection& connection, PlayerManager& player_manager, PacketDispatcher& dispatcher,
                AnimationSystem& animation);

  void Initialize(SpriteRenderer& renderer);

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);

  void OnWeaponPacket(u8* pkt, size_t size);

  void GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp, const Vector2f& position,
                      const Vector2f& velocity, const Vector2f& heading, u32 link_id);
};

}  // namespace null

#endif
