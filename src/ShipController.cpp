#include "ShipController.h"

#include <cstring>

#include "ArenaSettings.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"

namespace null {

ShipController::ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager)
    : player_manager(player_manager), weapon_manager(weapon_manager) {}

void ShipController::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  if (self == nullptr || self->ship >= 8) return;

  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

  // TODO: Real calculations with ship status
  u8 direction = (u8)(self->orientation * 40.0f);
  if (input.IsDown(InputAction::Forward)) {
    self->velocity += OrientationToHeading(direction) * (ship_settings.MaximumThrust * (10.0f / 16.0f)) * dt;
  }
  if (input.IsDown(InputAction::Backward)) {
    self->velocity -= OrientationToHeading(direction) * (ship_settings.MaximumThrust * (10.0f / 16.0f)) * dt;
  }

  if (input.IsDown(InputAction::Left)) {
    float rotation = ship_settings.MaximumRotation / 400.0f;
    self->orientation -= rotation * dt;
    if (self->orientation < 0) {
      self->orientation += 1.0f;
    }
  }

  if (input.IsDown(InputAction::Right)) {
    float rotation = ship_settings.MaximumRotation / 400.0f;
    self->orientation += rotation * dt;
    if (self->orientation >= 1.0f) {
      self->orientation -= 1.0f;
    }
  }

  self->velocity.Truncate(ship_settings.MaximumSpeed / 10.0f / 16.0f);

  FireWeapons(*self, input, dt);
}

void ShipController::FireWeapons(Player& self, const InputState& input, float dt) {
  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self.ship];
  u32 tick = GetCurrentTick();

  memset(&self.weapon, 0, sizeof(self.weapon));
  bool used_weapon = false;

  if (input.IsDown(InputAction::Bullet) && TICK_DIFF(tick, last_bullet_tick) >= ship_settings.BulletFireDelay) {
    // TODO: Real weapon data stored
    if (ship_settings.InitialGuns > 0) {
      self.weapon.level = ship_settings.MaxGuns - 1;
      if (connection.settings.PrizeWeights.BouncingBullets > 0) {
        self.weapon.type = (u16)WeaponType::BouncingBullet;
      } else {
        self.weapon.type = (u16)WeaponType::Bullet;
      }

      self.weapon.alternate = multifire;

      used_weapon = true;
      last_bullet_tick = tick;
    }
  } else if (input.IsDown(InputAction::Mine) && TICK_DIFF(tick, last_bomb_tick) >= ship_settings.BombFireDelay) {
    if (ship_settings.InitialBombs > 0) {
      self.weapon.level = ship_settings.MaxBombs - 1;
      self.weapon.type = (u16)WeaponType::Bomb;
      // TODO: Count mines
      self.weapon.alternate = 1;

      used_weapon = true;
      last_bomb_tick = tick;
    }
  } else if (input.IsDown(InputAction::Bomb) && TICK_DIFF(tick, last_bomb_tick) >= ship_settings.BombFireDelay) {
    if (ship_settings.InitialBombs > 0) {
      self.weapon.level = ship_settings.MaxBombs - 1;
      self.weapon.type = (u16)WeaponType::Bomb;

      used_weapon = true;
      last_bomb_tick = tick;
    }
  }

  if (used_weapon) {
    if (connection.map.GetTileId((u16)self.position.x, (u16)self.position.y) == kTileSafe) {
      self.velocity = Vector2f(0, 0);
      memset(&self.weapon, 0, sizeof(self.weapon));
    } else {
      weapon_manager.FireWeapons(self, self.weapon, self.position, self.velocity, GetCurrentTick());
      player_manager.SendPositionPacket();
    }
  }
}

void ShipController::Render(Camera& camera, SpriteRenderer& renderer) {
  // TODO: Render exhaust
}

}  // namespace null
