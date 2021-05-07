#ifndef NULLSPACE_SHIPCONTROLLER_H_
#define NULLSPACE_SHIPCONTROLLER_H_

#include "Types.h"

namespace null {

struct Camera;
struct InputState;
struct Player;
struct PlayerManager;
struct SpriteRenderer;
struct WeaponManager;

struct ShipController {
  PlayerManager& player_manager;
  WeaponManager& weapon_manager;
  u32 next_bullet_tick = 0;
  u32 next_bomb_tick = 0;
  bool multifire = false;

  ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager);

  void Update(const InputState& input, float dt);
  void FireWeapons(Player& self, const InputState& input, float dt);

  void Render(Camera& ui_camera, Camera& camera, SpriteRenderer& renderer);
};

}  // namespace null

#endif
