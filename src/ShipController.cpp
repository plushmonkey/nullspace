#include "ShipController.h"

#include <cassert>
#include <cstring>

#include "ArenaSettings.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "Random.h"
#include "Tick.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Animation.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

static void OnPlayerFreqAndShipChangePkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  controller->OnPlayerFreqAndShipChange(pkt, size);
}

static void OnCollectedPrizePkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  controller->OnCollectedPrize(pkt, size);
}

static void OnPlayerEnterPkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  u8 ship = *(pkt + 1);
  u16 player_id = *(u16*)(pkt + 51);

  if (ship != 8 && player_id == controller->player_manager.player_id) {
    controller->player_manager.Spawn();
  }
}

ShipController::ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager,
                               PacketDispatcher& dispatcher)
    : player_manager(player_manager), weapon_manager(weapon_manager) {
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::CollectedPrize, OnCollectedPrizePkt, this);
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
}

void ShipController::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  if (self == nullptr || self->ship >= 8 || self->enter_delay > 0.0f) {
    exhaust_count = 0;
    return;
  }

  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

  self->energy += (ship.recharge / 10.0f) * dt;
  if (self->energy > ship.energy) {
    self->energy = (float)ship.energy;
  }

  u8 direction = (u8)(self->orientation * 40.0f);
  bool thrust_backward = false;
  bool thrust_forward = false;

  if (input.IsDown(InputAction::Backward)) {
    self->velocity -= OrientationToHeading(direction) * (ship.thrust * (10.0f / 16.0f)) * dt;
    thrust_backward = true;
  } else if (input.IsDown(InputAction::Forward)) {
    self->velocity += OrientationToHeading(direction) * (ship.thrust * (10.0f / 16.0f)) * dt;
    thrust_forward = true;
  }

  if (input.IsDown(InputAction::Left)) {
    float rotation = ship.rotation / 400.0f;
    self->orientation -= rotation * dt;
    if (self->orientation < 0) {
      self->orientation += 1.0f;
    }
  }

  if (input.IsDown(InputAction::Right)) {
    float rotation = ship.rotation / 400.0f;
    self->orientation += rotation * dt;
    if (self->orientation >= 1.0f) {
      self->orientation -= 1.0f;
    }
  }

  self->velocity.Truncate(ship.speed / 10.0f / 16.0f);

  if (player_manager.connection.map.GetTileId(self->position) == kTileSafeId) {
    self->togglables |= Status_Safety;
  } else {
    self->togglables &= ~Status_Safety;
  }

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

  bool in_safe = connection.map.GetTileId(self.position) == kTileSafeId;

  // TODO: EMP item delays aren't the same as normal bomb tick

  if (input.IsDown(InputAction::Repel)) {
    if (TICK_GT(tick, next_bomb_tick)) {
      if (ship.repels > 0 && !in_safe) {
        self.weapon.type = (u16)WeaponType::Repel;
        --ship.repels;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
    }
  } else if (input.IsDown(InputAction::Burst)) {
    if (TICK_GT(tick, next_bomb_tick)) {
      if (ship.bursts > 0 && !in_safe) {
        self.weapon.type = (u16)WeaponType::Burst;
        --ship.bursts;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
    }
  } else if (input.IsDown(InputAction::Thor)) {
    if (TICK_GT(tick, next_bomb_tick)) {
      if (ship.thors > 0 && !in_safe) {
        self.weapon.type = (u16)WeaponType::Thor;
        --ship.thors;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
    }
  } else if (input.IsDown(InputAction::Decoy)) {
    if (TICK_GT(tick, next_bomb_tick)) {
      if (ship.decoys > 0 && !in_safe) {
        self.weapon.type = (u16)WeaponType::Decoy;
        --ship.decoys;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
    }
  } else if (input.IsDown(InputAction::Bullet) && TICK_GT(tick, next_bullet_tick)) {
    // TODO: Real weapon data stored
    if (ship.guns > 0) {
      self.weapon.level = ship.guns - 1;

      if (ship.capability & ShipCapability_BouncingBullets) {
        self.weapon.type = (u16)WeaponType::BouncingBullet;
      } else {
        self.weapon.type = (u16)WeaponType::Bullet;
      }

      self.weapon.alternate = ship.multifire && (ship.capability & ShipCapability_Multifire);

      used_weapon = true;
      if (self.weapon.alternate) {
        next_bullet_tick = tick + ship_settings.MultiFireDelay;
        energy_cost = ship_settings.MultiFireEnergy * (self.weapon.level + 1);
      } else {
        next_bullet_tick = tick + ship_settings.BulletFireDelay;
        energy_cost = ship_settings.BulletFireEnergy * (self.weapon.level + 1);
      }
    }
  } else if (input.IsDown(InputAction::Mine) && TICK_GT(tick, next_bomb_tick)) {
    if (ship.bombs > 0) {
      self.weapon.level = ship.bombs - 1;
      self.weapon.type =
          (u16)((ship.capability & ShipCapability_Proximity) ? WeaponType::ProximityBomb : WeaponType::Bomb);
      // TODO: Count mines
      self.weapon.alternate = 1;

      if (ship.guns > 0) {
        self.weapon.shrap = ship.shrapnel;
        self.weapon.shraplevel = ship.guns - 1;
        self.weapon.shrapbouncing = (ship.capability & ShipCapability_BouncingBullets) > 0;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
      energy_cost =
          ship_settings.LandmineFireEnergy + ship_settings.LandmineFireEnergyUpgrade * (self.weapon.level + 1);
    }
  } else if (input.IsDown(InputAction::Bomb) && TICK_GT(tick, next_bomb_tick)) {
    if (ship.bombs > 0) {
      self.weapon.level = ship.bombs - 1;
      self.weapon.type =
          (u16)((ship.capability & ShipCapability_Proximity) ? WeaponType::ProximityBomb : WeaponType::Bomb);

      if (ship.guns > 0) {
        self.weapon.shrap = ship.shrapnel;
        self.weapon.shraplevel = ship.guns - 1;
        self.weapon.shrapbouncing = (ship.capability & ShipCapability_BouncingBullets) > 0;
      }

      used_weapon = true;
      next_bomb_tick = tick + ship_settings.BombFireDelay;
      energy_cost = ship_settings.BombFireEnergy + ship_settings.BombFireEnergyUpgrade * (self.weapon.level + 1);
    }
  }

  if (used_weapon) {
    if (self.togglables & Status_Cloak) {
      self.togglables &= ~Status_Cloak;
      self.togglables |= Status_Flash;
    }

    if (connection.map.GetTileId(self.position) == kTileSafeId) {
      self.velocity = Vector2f(0, 0);
    } else if (self.energy > energy_cost) {
      u32 x = (u32)(self.position.x * 16);
      u32 y = (u32)(self.position.y * 16);
      s32 vel_x = (s32)(self.velocity.x * 16.0f * 10.0f);
      s32 vel_y = (s32)(self.velocity.y * 16.0f * 10.0f);

      weapon_manager.FireWeapons(self, self.weapon, x, y, vel_x, vel_y, GetCurrentTick());
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

  RenderIndicators(ui_camera, renderer);

  renderer.Render(ui_camera);

  for (size_t i = 0; i < exhaust_count; ++i) {
    Exhaust* exhaust = exhausts + i;

    renderer.Draw(camera, exhaust->animation.GetFrame(), exhaust->animation.position, Layer::AfterWeapons);
  }

  renderer.Render(camera);
}

void ShipController::RenderIndicators(Camera& ui_camera, SpriteRenderer& renderer) {
  Player* self = player_manager.GetSelf();

  if (!self) return;

  // TODO: Find real position
  float y_top = ((ui_camera.surface_dim.y * 0.57f) + 1.0f) - 25.0f * 4;
  float y = y_top;

  RenderItemIndicator(ui_camera, renderer, ship.bursts, 30, &y);
  RenderItemIndicator(ui_camera, renderer, ship.repels, 31, &y);
  RenderItemIndicator(ui_camera, renderer, ship.decoys, 40, &y);
  RenderItemIndicator(ui_camera, renderer, ship.thors, 41, &y);
  RenderItemIndicator(ui_camera, renderer, ship.bricks, 42, &y);
  RenderItemIndicator(ui_camera, renderer, ship.rockets, 43, &y);
  RenderItemIndicator(ui_camera, renderer, ship.portals, 46, &y);

  float x = ui_camera.surface_dim.x - 26;
  y = y_top;
  size_t gun_index = GetGunIconIndex();
  if (gun_index != 0xFFFFFFFF) {
    renderer.Draw(ui_camera, Graphics::icon_sprites[gun_index], Vector2f(x, y), Layer::Gauges);
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;

  size_t bomb_index = GetBombIconIndex();
  if (bomb_index != 0xFFFFFFFF) {
    renderer.Draw(ui_camera, Graphics::icon_sprites[bomb_index], Vector2f(x, y), Layer::Gauges);
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;

  if (ship.capability & ShipCapability_Stealth) {
    if (self->togglables & Status_Stealth) {
      renderer.Draw(ui_camera, Graphics::icon_sprites[32], Vector2f(x, y), Layer::Gauges);
    } else {
      renderer.Draw(ui_camera, Graphics::icon_sprites[33], Vector2f(x, y), Layer::Gauges);
    }
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;

  if (ship.capability & ShipCapability_Cloak) {
    if (self->togglables & Status_Cloak) {
      renderer.Draw(ui_camera, Graphics::icon_sprites[34], Vector2f(x, y), Layer::Gauges);
    } else {
      renderer.Draw(ui_camera, Graphics::icon_sprites[35], Vector2f(x, y), Layer::Gauges);
    }
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;

  if (ship.capability & ShipCapability_XRadar) {
    if (self->togglables & Status_XRadar) {
      renderer.Draw(ui_camera, Graphics::icon_sprites[36], Vector2f(x, y), Layer::Gauges);
    } else {
      renderer.Draw(ui_camera, Graphics::icon_sprites[37], Vector2f(x, y), Layer::Gauges);
    }
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;

  if (ship.capability & ShipCapability_Antiwarp) {
    if (self->togglables & Status_Antiwarp) {
      renderer.Draw(ui_camera, Graphics::icon_sprites[38], Vector2f(x, y), Layer::Gauges);
    } else {
      renderer.Draw(ui_camera, Graphics::icon_sprites[39], Vector2f(x, y), Layer::Gauges);
    }
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[1], Vector2f(x + 22, y), Layer::Gauges);
  }
  y += 25.0f;
}

void ShipController::RenderItemIndicator(Camera& ui_camera, SpriteRenderer& renderer, int value, size_t index,
                                         float* y) {
  if (value > 0) {
    renderer.Draw(ui_camera, Graphics::icon_sprites[index], Vector2f(0, *y), Layer::Gauges);

    if (value > 9) {
      renderer.Draw(ui_camera, Graphics::icon_count_sprites[10], Vector2f(23, *y + 5), Layer::Gauges);
    } else if (value > 1) {
      renderer.Draw(ui_camera, Graphics::icon_count_sprites[value], Vector2f(23, *y + 5), Layer::Gauges);
    }
  } else {
    renderer.Draw(ui_camera, Graphics::empty_icon_sprites[0], Vector2f(0, *y), Layer::Gauges);
  }

  *y += 25.0f;
}

size_t ShipController::GetGunIconIndex() {
  size_t start = 0;

  if (!ship.guns) {
    return 0xFFFFFFFF;
  }

  if (ship.capability & ShipCapability_Multifire) {
    if (ship.multifire) {
      start = 3;
    } else {
      start = 6;
    }
  }

  if (ship.capability & ShipCapability_BouncingBullets) {
    start += 9;
  }

  return start + ship.guns - 1;
}

size_t ShipController::GetBombIconIndex() {
  size_t start = 18;

  if (!ship.bombs) {
    return 0xFFFFFFFF;
  }

  if ((ship.capability & ShipCapability_Proximity) && !ship.shrapnel) {
    start += 3;
  } else if ((ship.capability & ShipCapability_Proximity) && ship.shrapnel) {
    start += 9;
  } else if (!(ship.capability & ShipCapability_Proximity) && ship.shrapnel) {
    start += 6;
  }

  return start + ship.bombs - 1;
}

void ShipController::OnPlayerFreqAndShipChange(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 new_ship = buffer.ReadU8();
  u16 pid = buffer.ReadU16();
  u16 freq = buffer.ReadU16();

  if (pid != player_manager.player_id) return;

  Player* player = player_manager.GetPlayerById(pid);

  if (player) {
    player_manager.Spawn();
  }
}

void ShipController::OnCollectedPrize(u8* pkt, size_t size) {
  u16 count = *(u16*)(pkt + 1);
  s16 prize_id = *(s16*)(pkt + 3);

  Player* self = player_manager.GetSelf();

  if (!self) return;

  for (u16 i = 0; i < count; ++i) {
    ApplyPrize(self, prize_id);
  }
}

void ShipController::ApplyPrize(Player* self, s32 prize_id) {
  bool negative = (prize_id < 0);
  Prize prize = (Prize)prize_id;

  if (negative) {
    prize = (Prize)(-prize_id);
    if (self->bounty > 0) {
      --self->bounty;
    }
  } else {
    ++self->bounty;
  }

  ShipSettings& ship_settings = player_manager.connection.settings.ShipSettings[self->ship];

  switch (prize) {
    case Prize::Recharge: {
      if (negative) {
        ship.recharge -= ship_settings.UpgradeRecharge;

        if (ship.recharge < ship_settings.InitialRecharge) {
          ship.recharge = ship_settings.InitialRecharge;
        }
      } else {
        ship.recharge += ship_settings.UpgradeRecharge;

        if (ship.recharge > ship_settings.MaximumRecharge) {
          ship.recharge = ship_settings.MaximumRecharge;
        }
      }
    } break;
    case Prize::Energy: {
      if (negative) {
        ship.energy -= ship_settings.UpgradeEnergy;

        if (ship.energy < ship_settings.InitialEnergy) {
          ship.energy = ship_settings.InitialEnergy;
        }
      } else {
        ship.energy += ship_settings.UpgradeEnergy;

        if (ship.energy > ship_settings.MaximumEnergy) {
          ship.energy = ship_settings.MaximumEnergy;
        }
      }
    } break;
    case Prize::Rotation: {
      if (negative) {
        ship.rotation -= ship_settings.UpgradeRotation;

        if (ship.rotation < ship_settings.InitialRotation) {
          ship.rotation = ship_settings.InitialRotation;
        }
      } else {
        ship.rotation += ship_settings.UpgradeRotation;

        if (ship.rotation > ship_settings.MaximumRotation) {
          ship.rotation = ship_settings.MaximumRotation;
        }
      }
    } break;
    case Prize::Stealth: {
      if (negative) {
        ship.capability &= ~ShipCapability_Stealth;
      } else {
        if (ship_settings.StealthStatus > 0) {
          ship.capability |= ShipCapability_Stealth;
        }
      }
    } break;
    case Prize::Cloak: {
      if (negative) {
        ship.capability &= ~ShipCapability_Cloak;
      } else {
        if (ship_settings.CloakStatus > 0) {
          ship.capability |= ShipCapability_Cloak;
        }
      }
    } break;
    case Prize::XRadar: {
      if (negative) {
        ship.capability &= ~ShipCapability_XRadar;
      } else {
        if (ship_settings.XRadarStatus > 0) {
          ship.capability |= ShipCapability_XRadar;
        }
      }
    } break;
    case Prize::Warp: {
      player_manager.Spawn();
    } break;
    case Prize::Guns: {
      if (negative) {
        --ship.guns;

        if (ship.guns < ship_settings.InitialGuns) {
          ship.guns = ship_settings.InitialGuns;
        }
      } else {
        ++ship.guns;

        if (ship.guns > ship_settings.MaxGuns) {
          ship.guns = ship_settings.MaxGuns;
        }
      }
    } break;
    case Prize::Bombs: {
      if (negative) {
        --ship.bombs;

        if (ship.bombs < ship_settings.InitialBombs) {
          ship.bombs = ship_settings.InitialBombs;
        }
      } else {
        ++ship.bombs;

        if (ship.bombs > ship_settings.MaxBombs) {
          ship.bombs = ship_settings.MaxBombs;
        }
      }
    } break;
    case Prize::BouncingBullets: {
      if (negative) {
        ship.capability &= ~ShipCapability_BouncingBullets;
      } else {
        if (ship.capability & ShipCapability_BouncingBullets) {
          --self->bounty;
        }
        ship.capability |= ShipCapability_BouncingBullets;
      }
    } break;
    case Prize::Thruster: {
      if (negative) {
        ship.thrust -= ship_settings.UpgradeThrust;

        if (ship.thrust < ship_settings.InitialThrust) {
          ship.thrust = ship_settings.InitialThrust;
        }
      } else {
        ship.thrust += ship_settings.UpgradeThrust;

        if (ship.thrust > ship_settings.MaximumThrust) {
          ship.thrust = ship_settings.MaximumThrust;
        }
      }
    } break;
    case Prize::TopSpeed: {
      if (negative) {
        ship.speed -= ship_settings.UpgradeSpeed;

        if (ship.speed < ship_settings.InitialSpeed) {
          ship.speed = ship_settings.InitialSpeed;
        }
      } else {
        ship.speed += ship_settings.UpgradeSpeed;

        if (ship.speed > ship_settings.MaximumSpeed) {
          ship.speed = ship_settings.MaximumSpeed;
        }
      }
    } break;
    case Prize::FullCharge: {
      self->energy = (float)ship.energy;
    } break;
    case Prize::EngineShutdown: {
      // TODO: Implement
    } break;
    case Prize::Multifire: {
      if (negative) {
        ship.capability &= ~ShipCapability_Multifire;
      } else {
        ship.capability |= ShipCapability_Multifire;
      }
    } break;
    case Prize::Proximity: {
      if (negative) {
        ship.capability &= ~ShipCapability_Proximity;
      } else {
        if (ship.capability & ShipCapability_Proximity) {
          --self->bounty;
        }
        ship.capability |= ShipCapability_Proximity;
      }
    } break;
    case Prize::Super: {
      // TODO: Implement
    } break;
    case Prize::Shields: {
      // TODO: Implement
    } break;
    case Prize::Shrapnel: {
      if (negative) {
        if (ship.shrapnel >= ship_settings.ShrapnelRate) {
          ship.shrapnel -= ship_settings.ShrapnelRate;
        }
      } else {
        ship.shrapnel += ship_settings.ShrapnelRate;

        if (ship.shrapnel > ship_settings.ShrapnelMax) {
          ship.shrapnel = ship_settings.ShrapnelMax;
        }
      }
    } break;
    case Prize::Antiwarp: {
      if (negative) {
        ship.capability &= ~ShipCapability_Antiwarp;
      } else {
        if (ship_settings.AntiWarpStatus > 0) {
          ship.capability |= ShipCapability_Antiwarp;
        }
      }
    } break;
    case Prize::Repel: {
      if (negative) {
        if (ship.repels > 0) {
          --ship.repels;
        }
      } else {
        ++ship.repels;
        if (ship.repels > ship_settings.RepelMax) {
          ship.repels = ship_settings.RepelMax;
        }
      }
    } break;
    case Prize::Burst: {
      if (negative) {
        if (ship.bursts > 0) {
          --ship.bursts;
        }
      } else {
        ++ship.bursts;
        if (ship.bursts > ship_settings.BurstMax) {
          ship.bursts = ship_settings.BurstMax;
        }
      }
    } break;
    case Prize::Decoy: {
      if (negative) {
        if (ship.decoys > 0) {
          --ship.decoys;
        }
      } else {
        ++ship.decoys;
        if (ship.decoys > ship_settings.DecoyMax) {
          ship.decoys = ship_settings.DecoyMax;
        }
      }
    } break;
    case Prize::Thor: {
      if (negative) {
        if (ship.thors > 0) {
          --ship.thors;
        }
      } else {
        ++ship.thors;
        if (ship.thors > ship_settings.ThorMax) {
          ship.thors = ship_settings.ThorMax;
        }
      }
    } break;
    case Prize::Multiprize: {
      // TODO: Implement
    } break;
    case Prize::Brick: {
      if (negative) {
        if (ship.bricks > 0) {
          --ship.bricks;
        }
      } else {
        ++ship.bricks;
        if (ship.bricks > ship_settings.BrickMax) {
          ship.bricks = ship_settings.BrickMax;
        }
      }
    } break;
    case Prize::Rocket: {
      if (negative) {
        if (ship.rockets > 0) {
          --ship.rockets;
        }
      } else {
        ++ship.rockets;
        if (ship.rockets > ship_settings.RocketMax) {
          ship.rockets = ship_settings.RocketMax;
        }
      }
    } break;
    case Prize::Portal: {
      if (negative) {
        if (ship.portals > 0) {
          --ship.bursts;
        }
      } else {
        ++ship.portals;
        if (ship.portals > ship_settings.PortalMax) {
          ship.portals = ship_settings.PortalMax;
        }
      }
    } break;
    default: {
    } break;
  }
}

s32 ShipController::GeneratePrize(bool negative_allowed) {
  u32 weight_total = player_manager.connection.prize_weight_total;

  if (weight_total <= 0) return 0;

  u8* weights = (u8*)&player_manager.connection.settings.PrizeWeights;
  VieRNG rng = {(s32)player_manager.connection.security.prize_seed};

  u32 random = rng.GetNext();
  s32 result = 0;
  u32 weight = 0;

  for (int prize_id = 0; prize_id < sizeof(player_manager.connection.settings.PrizeWeights); ++prize_id) {
    weight += weights[prize_id];

    if (random % weight_total < weight) {
      random = rng.GetNext();

      if (!negative_allowed || random % player_manager.connection.settings.PrizeNegativeFactor != 0) {
        result = prize_id + 1;
        break;
      }

      result = -(prize_id + 1);
      break;
    }
  }

  player_manager.connection.security.prize_seed = random;
  return result;
}

void ShipController::ResetShip() {
  Player* self = player_manager.GetSelf();

  if (!self) return;

  ShipSettings& ship_settings = player_manager.connection.settings.ShipSettings[self->ship];

  ship.energy = ship_settings.InitialEnergy;
  ship.recharge = ship_settings.InitialRecharge;
  ship.rotation = ship_settings.InitialRotation;
  ship.guns = ship_settings.InitialGuns;
  ship.bombs = ship_settings.InitialBombs;
  ship.thrust = ship_settings.InitialThrust;
  ship.speed = ship_settings.InitialSpeed;
  ship.shrapnel = 0;
  ship.repels = ship_settings.InitialRepel;
  ship.bursts = ship_settings.InitialBurst;
  ship.decoys = ship_settings.InitialDecoy;
  ship.thors = ship_settings.InitialThor;
  ship.bricks = ship_settings.InitialBrick;
  ship.rockets = ship_settings.InitialRocket;
  ship.portals = ship_settings.InitialPortal;
  ship.capability = 0;
  ship.emped_time = 0.0f;
  ship.multifire = false;

  self->togglables = 0;

  if (ship_settings.StealthStatus == 2) {
    ship.capability |= ShipCapability_Stealth;
  }

  if (ship_settings.CloakStatus == 2) {
    ship.capability |= ShipCapability_Cloak;
  }

  if (ship_settings.XRadarStatus == 2) {
    ship.capability |= ShipCapability_XRadar;
  }

  if (ship_settings.AntiWarpStatus == 2) {
    ship.capability |= ShipCapability_Antiwarp;
  }

  self->bounty = 0;

  u32 pristine_seed = player_manager.connection.security.prize_seed;

  // Generate random weighted prizes
  if (player_manager.connection.prize_weight_total > 0) {
    int attempts = 0;
    for (int i = 0; i < ship_settings.InitialBounty && attempts < 9999; ++i, ++attempts) {
      s32 prize_id = GeneratePrize(false);
      Prize prize = (Prize)prize_id;

      if (prize == Prize::FullCharge || prize == Prize::EngineShutdown || prize == Prize::Shields ||
          prize == Prize::Super || prize == Prize::Warp || prize == Prize::Brick) {
        --i;
        continue;
      }

      ApplyPrize(self, prize_id);
    }
  }

  // Restore the prize seed to maintain synchronization with other clients.
  // The GeneratePrizes called above would mutate the seed, so it should be restored.
  player_manager.connection.security.prize_seed = pristine_seed;

  self->energy = (float)ship.energy;
  self->bounty = ship_settings.InitialBounty;
}

void ShipController::OnWeaponHit(Weapon& weapon) {
  WeaponType type = (WeaponType)weapon.data.type;
  Connection& connection = player_manager.connection;

  Player* self = player_manager.GetSelf();

  if (!self || self->enter_delay > 0 || self->ship == 8) return;

  Player* shooter = player_manager.GetPlayerById(weapon.player_id);

  if (!shooter) return;

  int damage = 0;

  switch (type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      if (weapon.data.shrap > 0) {
        s32 remaining = weapon.end_tick - GetCurrentTick();
        s32 duration = connection.settings.BulletAliveTime - remaining;

        if (duration <= 25) {
          damage = connection.settings.InactiveShrapDamage / 1000;
        } else {
          float multiplier = connection.settings.ShrapnelDamagePercent / 1000.0f;

          damage = (connection.settings.BulletDamageLevel / 1000) +
                   (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;

          damage = (int)(damage * multiplier);
        }
      } else {
        damage = (connection.settings.BulletDamageLevel / 1000) +
                 (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;
      }
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      int bomb_dmg = connection.settings.BombDamageLevel;
      int level = weapon.data.level;

      if (type == WeaponType::Thor) {
        // Weapon level should always be 0 for thor in normal gameplay, I believe, but this is how it's done
        bomb_dmg = bomb_dmg + bomb_dmg * weapon.data.level * weapon.data.level;
        level = 3 + weapon.data.level;
      }

      bomb_dmg = bomb_dmg / 1000;

      if (weapon.flags & WEAPON_FLAG_EMP) {
        bomb_dmg = (int)(bomb_dmg * (connection.settings.EBombDamagePercent / 1000.0f));
      }

      if (connection.settings.ShipSettings[shooter->ship].BombBounceCount > 0) {
        bomb_dmg = (int)(bomb_dmg * (connection.settings.BBombDamagePercent / 1000.0f));
      }

      Vector2f delta = Absolute(weapon.position - self->position) * 16.0f;

      float explode_pixels =
          (float)(connection.settings.BombExplodePixels + connection.settings.BombExplodePixels * level);

      if (delta.LengthSq() < explode_pixels * explode_pixels) {
        float distance = delta.Length();

        damage = (int)((explode_pixels - distance) * (bomb_dmg / explode_pixels));

        if (self->id != shooter->id) {
          Vector2f shooter_delta = Absolute(weapon.position - shooter->position) * 16;
          float shooter_distance = shooter_delta.Length();

          if (shooter_distance < explode_pixels) {
            damage -= (int)(((bomb_dmg / explode_pixels) * (explode_pixels - shooter_distance)) / 2.0f);

            if (damage < 0) {
              damage = 0;
            }
          }
        }

        if ((weapon.flags & WEAPON_FLAG_EMP) && damage > 0 && self->id != shooter->id) {
          TileId tile_id = connection.map.GetTileId((u16)self->position.x, (u16)self->position.y);

          if (tile_id != kTileSafeId) {
            u32 emp_time = (u32)((connection.settings.EBombShutdownTime * damage) / damage);
            // TODO: Set emp time
          }
        }
      }
    } break;
    case WeaponType::Burst: {
      damage = connection.settings.BurstDamageLevel;
    } break;
    default: {
    } break;
  }

  if (!connection.settings.ExactDamage &&
      (type == WeaponType::Bullet || type == WeaponType::BouncingBullet || type == WeaponType::Burst)) {
    u32 r = (rand() * 1000) % (damage * damage + 1);
    damage = (s32)sqrt(r);
  }

  if (damage <= 0) return;

  if (self->energy < damage) {
    bool is_bomb = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb || type == WeaponType::Thor);

    if (is_bomb && shooter->id == self->id) {
      self->energy = 1.0f;
    } else {
      connection.SendDeath(weapon.player_id, self->bounty);

      self->enter_delay = (connection.settings.EnterDelay / 100.0f) + self->explode_animation.sprite->duration;
      self->explode_animation.t = 0.0f;
      self->energy = 0;
    }
  } else {
    self->energy -= damage;
  }
}

}  // namespace null
