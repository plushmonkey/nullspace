#ifndef NULLSPACE_WEAPONMANAGER_H_
#define NULLSPACE_WEAPONMANAGER_H_

#include "Player.h"
#include "Types.h"
#include "render/Animation.h"

namespace null {

enum class WeaponType { None, Bullet, BouncingBullet, Bomb, ProximityBomb, Repel, Decoy, Burst, Thor };
constexpr u32 kInvalidLink = 0xFFFFFFFF;

#define WEAPON_FLAG_EMP (1 << 0)
#define WEAPON_FLAG_BURST_ACTIVE (1 << 1)

struct Weapon {
  Vector2f position;
  Vector2f velocity;

  u32 last_tick;
  u32 end_tick;

  u16 player_id;
  WeaponData data;

  u16 frequency;

  union {
    struct {
      // Player id for delayed prox explosions
      u16 prox_hit_player_id;

      // Highest of dx or dy when prox was triggered
      float prox_highest_offset;

      u32 sensor_end_tick;
      u32 rng_seed;
    };
    float initial_orientation;
  };

  u32 last_trail_tick;
  u32 bounces_remaining = 0;

  u32 flags;

  // incremental id for connected multifire bullet
  u32 link_id = kInvalidLink;

  Animation animation;
};

struct Camera;
struct Connection;
struct PacketDispatcher;
struct PlayerManager;
struct ShipController;
struct SpriteRenderer;

enum class WeaponSimulateResult { Continue, WallExplosion, PlayerExplosion, TimedOut };

// TODO: this could be a single u32 if performance is ever a concern
struct WeaponLinkRemoval {
  u32 link_id;
  WeaponSimulateResult result;
};

struct WeaponManager {
  Connection& connection;
  PlayerManager& player_manager;
  AnimationSystem& animation;
  ShipController* ship_controller = nullptr;
  u32 next_link_id = 0;

  size_t weapon_count = 0;
  Weapon weapons[65535];

  size_t link_removal_count = 0;
  WeaponLinkRemoval link_removals[2048];

  WeaponManager(Connection& connection, PlayerManager& player_manager, PacketDispatcher& dispatcher,
                AnimationSystem& animation);

  void Initialize(ShipController* ship_controller) { this->ship_controller = ship_controller; }

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);
  WeaponSimulateResult Simulate(Weapon& weapon, u32 current_tick);

  bool SimulateAxis(Weapon& weapon, float dt, int axis);
  WeaponSimulateResult SimulatePosition(Weapon& weapon);

  void AddLinkRemoval(u32 link_id, WeaponSimulateResult result);
  bool HasLinkRemoved(u32 link_id);

  void CreateExplosion(Weapon& weapon);

  u32 CalculateRngSeed(u32 x, u32 y, u32 vel_x, u32 vel_y, u16 shrap_count, u16 weapon_level, u32 frequency);

  void OnWeaponPacket(u8* pkt, size_t size);
  void FireWeapons(Player& player, WeaponData weapon, u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y, u32 timestamp);

  WeaponSimulateResult GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp, u32 pos_x, u32 pos_y,
                                      s32 vel_x, s32 vel_y, const Vector2f& heading, u32 link_id);

  void ClearWeapons(Player& player);
};

}  // namespace null

#endif
