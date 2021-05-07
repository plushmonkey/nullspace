#include "ShipController.h"

#include <cstring>

#include "ArenaSettings.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

ShipController::ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager)
    : player_manager(player_manager), weapon_manager(weapon_manager) {}

void ShipController::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  if (self == nullptr || self->ship >= 8) return;
  if (self->enter_delay > 0.0f) return;

  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

  self->energy += (ship_settings.InitialRecharge / 10.0f) * dt;
  if (self->energy > ship_settings.InitialEnergy) {
    self->energy = ship_settings.InitialEnergy;
  }

  // TODO: Real calculations with ship status
  u8 direction = (u8)(self->orientation * 40.0f);
  if (input.IsDown(InputAction::Forward)) {
    self->velocity += OrientationToHeading(direction) * (ship_settings.InitialThrust * (10.0f / 16.0f)) * dt;
  }
  if (input.IsDown(InputAction::Backward)) {
    self->velocity -= OrientationToHeading(direction) * (ship_settings.InitialThrust * (10.0f / 16.0f)) * dt;
  }

  if (input.IsDown(InputAction::Left)) {
    float rotation = ship_settings.InitialRotation / 400.0f;
    self->orientation -= rotation * dt;
    if (self->orientation < 0) {
      self->orientation += 1.0f;
    }
  }

  if (input.IsDown(InputAction::Right)) {
    float rotation = ship_settings.InitialRotation / 400.0f;
    self->orientation += rotation * dt;
    if (self->orientation >= 1.0f) {
      self->orientation -= 1.0f;
    }
  }

  self->velocity.Truncate(ship_settings.InitialSpeed / 10.0f / 16.0f);

  FireWeapons(*self, input, dt);
}

void ShipController::FireWeapons(Player& self, const InputState& input, float dt) {
  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self.ship];
  u32 tick = GetCurrentTick();

  memset(&self.weapon, 0, sizeof(self.weapon));
  bool used_weapon = false;

  u16 energy_cost = 0;

  if (input.IsDown(InputAction::Bullet) && TICK_GT(tick, next_bullet_tick)) {
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
      if (multifire) {
        next_bullet_tick = tick + ship_settings.MultiFireDelay;
        energy_cost = ship_settings.MultiFireEnergy * (self.weapon.level + 1);
      } else {
        next_bullet_tick = tick + ship_settings.BulletFireDelay;
        energy_cost = ship_settings.BulletFireEnergy * (self.weapon.level + 1);
      }
    }
  } else if (input.IsDown(InputAction::Mine) && TICK_GT(tick, next_bomb_tick)) {
    if (ship_settings.InitialBombs > 0) {
      self.weapon.level = ship_settings.MaxBombs - 1;
      self.weapon.type = (u16)WeaponType::Bomb;
      // TODO: Count mines
      self.weapon.alternate = 1;

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
      energy_cost =
          ship_settings.LandmineFireEnergy + ship_settings.LandmineFireEnergyUpgrade * (self.weapon.level + 1);
    }
  } else if (input.IsDown(InputAction::Bomb) && TICK_GT(tick, next_bomb_tick)) {
    if (ship_settings.InitialBombs > 0) {
      self.weapon.level = ship_settings.MaxBombs - 1;
      self.weapon.type = (u16)WeaponType::Bomb;

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
      energy_cost = ship_settings.BombFireEnergy + ship_settings.BombFireEnergyUpgrade * (self.weapon.level + 1);
    }
  }

  if (used_weapon) {
    if (connection.map.GetTileId((u16)self.position.x, (u16)self.position.y) == kTileSafe) {
      self.velocity = Vector2f(0, 0);
      memset(&self.weapon, 0, sizeof(self.weapon));
    } else if (self.energy > energy_cost) {
      weapon_manager.FireWeapons(self, self.weapon, self.position, self.velocity, GetCurrentTick());
      self.energy -= energy_cost;
      player_manager.SendPositionPacket();
    }
  }
}

void ShipController::Render(Camera& ui_camera, Camera& camera, SpriteRenderer& renderer) {
  Player* self = player_manager.GetSelf();

  if (!self || self->ship == 8) return;

  int energy = (int)self->energy;

  int count = 0;
  while (energy > 0) {
    int digit = energy % 10;
    SpriteRenderable& renderable = Graphics::energyfont_sprites[digit];

    renderer.Draw(ui_camera, renderable, Vector2f(ui_camera.surface_dim.x - (++count * 16), 0), Layer::Gauges);

    energy /= 10;
  }

  renderer.Render(ui_camera);

  // TODO: Render exhaust
}

}  // namespace null
