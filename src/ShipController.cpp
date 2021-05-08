#include "ShipController.h"

#include <cassert>
#include <cstring>

#include "ArenaSettings.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "render/Animation.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

ShipController::ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager)
    : player_manager(player_manager), weapon_manager(weapon_manager) {}

void ShipController::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  if (self == nullptr || self->ship >= 8 || self->enter_delay > 0.0f) {
    exhaust_count = 0;
    return;
  }

  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

  self->energy += (ship_settings.MaximumRecharge / 10.0f) * dt;
  if (self->energy > ship_settings.MaximumEnergy) {
    self->energy = ship_settings.MaximumEnergy;
  }

  // TODO: Real calculations with ship status
  u8 direction = (u8)(self->orientation * 40.0f);
  bool thrust_backward = false;
  bool thrust_forward = false;

  if (input.IsDown(InputAction::Backward)) {
    self->velocity -= OrientationToHeading(direction) * (ship_settings.MaximumThrust * (10.0f / 16.0f)) * dt;
    thrust_backward = true;
  } else if (input.IsDown(InputAction::Forward)) {
    self->velocity += OrientationToHeading(direction) * (ship_settings.MaximumThrust * (10.0f / 16.0f)) * dt;
    thrust_forward = true;
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

  for (size_t i = 0; i < exhaust_count; ++i) {
    Exhaust* exhaust = exhausts + i;

    bool moving = exhaust->animation.t <= 0.3f * exhaust->animation.sprite->duration;
    // Speed up the animation at the beginning
    exhaust->animation.t += moving ? dt * 2.0f : dt;

    if (!exhaust->animation.IsAnimating()) {
      exhausts[i--] = exhausts[--exhaust_count];
      continue;
    }

    if (moving) {
      exhaust->animation.position += exhaust->velocity * dt;
    }
  }

  static u32 last_tick = 0;
  u32 tick = GetCurrentTick();

  if ((thrust_forward || thrust_backward) && TICK_DIFF(tick, last_tick) >= 6) {
    Connection& connection = player_manager.connection;
    ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

    Vector2f exhaust_pos = self->position - Graphics::anim_ship_exhaust.frames[0].dimensions * (0.5f / 16.0f);
    Vector2f heading = OrientationToHeading((u8)(self->orientation * 40.0f));

    float velocity_strength = 12.0f;
    Vector2f velocity = (thrust_forward ? -heading : heading) * velocity_strength;

    Exhaust* exhaust_r = exhausts + exhaust_count++;
    assert(exhaust_count + 1 < NULLSPACE_ARRAY_SIZE(exhausts));

    exhaust_r->animation.t = 0.0f;
    exhaust_r->animation.position = (exhaust_pos + Perpendicular(heading) * 0.2f) - heading * ship_settings.GetRadius();
    exhaust_r->animation.sprite = &Graphics::anim_ship_exhaust;
    exhaust_r->velocity = velocity;
    exhaust_r->velocity += Perpendicular(heading) * (velocity_strength * 0.2f);

    Exhaust* exhaust_l = exhausts + exhaust_count++;
    exhaust_l->animation.t = 0.0f;
    exhaust_l->animation.position = (exhaust_pos - Perpendicular(heading) * 0.2f) - heading * ship_settings.GetRadius();
    exhaust_l->animation.sprite = &Graphics::anim_ship_exhaust;
    exhaust_l->velocity = velocity;
    exhaust_l->velocity -= Perpendicular(heading) * (velocity_strength * 0.2f);

    last_tick = tick;
  }
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
    } else if (self.energy > energy_cost) {
      weapon_manager.FireWeapons(self, self.weapon, self.position, self.velocity, GetCurrentTick());
      self.energy -= energy_cost;
      player_manager.SendPositionPacket();
    }
  }

  memset(&self.weapon, 0, sizeof(self.weapon));
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

  for (size_t i = 0; i < exhaust_count; ++i) {
    Exhaust* exhaust = exhausts + i;

    renderer.Draw(camera, exhaust->animation.GetFrame(), exhaust->animation.position, Layer::AfterWeapons);
  }

  renderer.Render(camera);
}

}  // namespace null
