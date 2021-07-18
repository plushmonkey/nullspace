#ifndef NULLSPACE_SHIPCONTROLLER_H_
#define NULLSPACE_SHIPCONTROLLER_H_

#include "Types.h"
#include "render/Animation.h"

namespace null {

struct Camera;
struct InputState;
struct NotificationSystem;
struct PacketDispatcher;
struct Player;
struct PlayerManager;
struct SpriteRenderer;
struct Weapon;
struct WeaponManager;

struct Exhaust {
  Animation animation;
  Vector2f velocity;

  // Monotonically increasing index to preserve animation order
  u32 index;
  // How far through the animation should it stop being moved
  float end_movement_t;
  // How fast the animation should advance after movement
  float end_animation_speed;
};

enum class Prize {
  None,
  Recharge,
  Energy,
  Rotation,
  Stealth,
  Cloak,
  XRadar,
  Warp,
  Guns,
  Bombs,
  BouncingBullets,
  Thruster,
  TopSpeed,
  FullCharge,
  EngineShutdown,
  Multifire,
  Proximity,
  Super,
  Shields,
  Shrapnel,
  Antiwarp,
  Repel,
  Burst,
  Decoy,
  Thor,
  Multiprize,
  Brick,
  Rocket,
  Portal,

  Count
};

enum {
  ShipCapability_Stealth = (1 << 0),
  ShipCapability_Cloak = (1 << 1),
  ShipCapability_XRadar = (1 << 2),
  ShipCapability_Antiwarp = (1 << 3),
  ShipCapability_Multifire = (1 << 4),
  ShipCapability_Proximity = (1 << 5),
  ShipCapability_BouncingBullets = (1 << 6),
};
using ShipCapabilityFlags = int;

struct Ship {
  u32 energy;
  u32 recharge;
  u32 rotation;
  u32 guns;
  u32 bombs;
  u32 thrust;
  u32 speed;
  u32 shrapnel;

  u32 repels;
  u32 bursts;
  u32 decoys;
  u32 thors;
  u32 bricks;
  u32 rockets;
  u32 portals;

  u32 next_bullet_tick = 0;
  u32 next_bomb_tick = 0;
  u32 next_repel_tick = 0;

  u32 rocket_end_tick;
  u32 shutdown_end_tick;
  u32 fake_antiwarp_end_tick;

  float emped_time;
  float super_time;
  float shield_time;

  float portal_time;
  Vector2f portal_location;

  bool multifire;

  ShipCapabilityFlags capability;
};

typedef void (*OnBombDamage)(void* user);

struct BombExplosionReport {
  OnBombDamage on_damage = nullptr;
  void* user = nullptr;
};

struct ShipController {
  PlayerManager& player_manager;
  WeaponManager& weapon_manager;
  NotificationSystem& notifications_;
  Ship ship;

  BombExplosionReport explosion_report;

  u32 last_emp_animation_tick = 0;
  u32 next_exhaust_index = 0;
  u32 next_exhaust_tick = 0;

  // Only activate warps/portals on key press
  bool portal_input_cleared = true;
  bool warp_input_cleared = true;

  Animation health_animation;
  Animation portal_animation;
  Animation super_animation;
  Animation shield_animation;
  Animation flag_animation;

  size_t exhaust_count = 0;
  Exhaust exhausts[64];

  ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager, PacketDispatcher& dispatcher,
                 NotificationSystem& notifications);

  void Update(const InputState& input, float dt);
  void UpdateEffects(float dt);
  void UpdateExhaust(Player& self, bool thrust_forward, bool thrust_backward, float dt);

  void FireWeapons(Player& self, const InputState& input, float dt, bool afterburners);
  void HandleStatusEnergy(Player& self, u32 status, u32 cost, float dt);

  void Render(Camera& ui_camera, Camera& camera, SpriteRenderer& renderer);
  void RenderEnergyDisplay(Camera& ui_camera, SpriteRenderer& renderer);
  void RenderIndicators(Camera& ui_camera, SpriteRenderer& renderer);
  void RenderItemIndicator(Camera& ui_camera, SpriteRenderer& renderer, int value, size_t index, float* y);
  size_t GetGunIconIndex();
  size_t GetBombIconIndex();

  void OnCollectedPrize(u8* pkt, size_t size);

  void ResetShip();
  void UpdateSettings();

  void ApplyPrize(Player* self, s32 prize_id, bool notify, bool damage = false);
  s32 GeneratePrize(bool negative_allowed);

  void OnWeaponHit(Weapon& weapon);

  // Creates an exhaust and spawns it directly behind the ship.
  Exhaust* CreateExhaust(const Vector2f& position, const Vector2f& heading, const Vector2f& velocity,
                         float ship_radius);
};

}  // namespace null

#endif
