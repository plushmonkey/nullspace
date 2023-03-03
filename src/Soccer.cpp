#include "Soccer.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "Clock.h"
#include "PlayerManager.h"
#include "Radar.h"
#include "ShipController.h"
#include "SpectateView.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "render/Animation.h"

namespace null {

static void OnPowerballPositionPkt(void* user, u8* pkt, size_t size) {
  Soccer* soccer = (Soccer*)user;

  soccer->OnPowerballPosition(pkt, size);
}

inline void SimulateAxis(Powerball& ball, Map& map, u32* pos, s16* vel) {
  u32 previous = *pos;

  *pos += *vel;

  float x = std::floor(ball.x / 16000.0f);
  float y = std::floor(ball.y / 16000.0f);

  if (map.IsSolid((u16)x, (u16)y, ball.frequency)) {
    *pos = previous;
    *vel = -*vel;
  }
}

inline Vector2f GetBallPosition(PlayerManager& player_manager, Powerball& ball, u64 microtick) {
  if (ball.state == BallState::Carried) {
    Player* carrier = player_manager.GetPlayerById(ball.carrier_id);

    if (carrier && carrier->ship != 8) {
      Vector2f heading = OrientationToHeading((u8)(carrier->orientation * 40.0f));
      float radius = player_manager.connection.settings.ShipSettings[carrier->ship].GetRadius();

      float extension = radius - 0.25f;

      if (extension < 0) {
        extension = 0.0f;
      }

      return carrier->position.PixelRounded() + heading * extension;
    }
  }

  Vector2f current_position(ball.x / 16000.0f, ball.y / 16000.0f);
  Vector2f next_position(ball.next_x / 16000.0f, ball.next_y / 16000.0f);
  float t = (microtick - ball.last_micro_tick) / (float)kTickDurationMicro;

  return current_position * (1 - t) + (t * next_position);
}

Soccer::Soccer(PlayerManager& player_manager) : player_manager(player_manager), connection(player_manager.connection) {
  connection.dispatcher.Register(ProtocolS2C::PowerballPosition, OnPowerballPositionPkt, this);

  Clear();
}

void Soccer::Render(Camera& camera, SpriteRenderer& renderer) {
  Animation animation;

  animation.t = anim_t;

  u64 microtick = GetMicrosecondTick();
  u32 tick = GetCurrentTick();

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    if (ball->id != kInvalidBallId) {
      animation.sprite = &Graphics::anim_powerball;

      if (ball->state == BallState::World && TICK_DIFF(tick, ball->last_touch_timestamp) < connection.settings.PassDelay) {
        animation.sprite = &Graphics::anim_powerball_phased;
      }

      SpriteRenderable& renderable = animation.GetFrame();

      Vector2f position = GetBallPosition(player_manager, *ball, microtick);
      Vector2f render_position = position - renderable.dimensions * (0.5f / 16.0f);

      renderer.Draw(camera, renderable, Vector3f(render_position, (float)Layer::AfterWeapons + kAnimationLayerTop));
      RenderIndicator(*ball, position);
    }
  }
}

void Soccer::RenderIndicator(Powerball& ball, const Vector2f& position) {
  if (ball.state == BallState::Carried) {
    Player* carrier = player_manager.GetPlayerById(ball.carrier_id);

    if (carrier) {
      u32 self_freq = player_manager.specview->GetFrequency();

      if (carrier->frequency == self_freq) {
        RadarIndicatorFlags flags =
            carrier->id == player_manager.player_id ? RadarIndicatorFlag_FullMap : RadarIndicatorFlag_All;

        player_manager.radar->AddTemporaryIndicator(position, 0, Vector2f(3, 3), ColorType::RadarTeam, flags);
      } else {
        player_manager.radar->AddTemporaryIndicator(position, 0, Vector2f(3, 3), ColorType::RadarEnemyFlag,
                                                    RadarIndicatorFlag_FullMap);
      }

      return;
    }
  }

  player_manager.radar->AddTemporaryIndicator(position, 0, Vector2f(2, 2), ColorType::RadarTeamFlag,
                                              RadarIndicatorFlag_All);
}

void Soccer::Update(float dt) {
  u64 microtick = GetMicrosecondTick();
  u32 tick = GetCurrentTick();

  s32 pass_delay = connection.settings.PassDelay;

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    if (ball->id == kInvalidBallId) continue;

    while ((s64)(microtick - ball->last_micro_tick) >= kTickDurationMicro) {
      Simulate(*ball, true);
      ball->last_micro_tick += kTickDurationMicro;
    }

    // Update timer if the carrier is this player
    if (ball->state == BallState::Carried && ball->carrier_id == player_manager.player_id) {
      carry_id = ball->id;
      carry_timer -= dt;

      Player* self = player_manager.GetSelf();
      if (self && self->ship != 8) {
        bool has_timer = connection.settings.ShipSettings[self->ship].SoccerBallThrowTimer > 0;

        if (has_timer && carry_timer < 0) {
          float speed = connection.settings.ShipSettings[self->ship].SoccerBallSpeed / 10.0f / 16.0f;
          Vector2f position = GetBallPosition(player_manager, *ball, microtick);
          Vector2f heading = OrientationToHeading((u8)(self->orientation * 40.0f));
          Vector2f velocity = self->velocity - Vector2f(heading) * speed;

          u32 timestamp = GetCurrentTick() + connection.time_diff;

          connection.SendBallFire((u8)ball->id, position, velocity, self->id, timestamp);
          carry_id = kInvalidBallId;
        }
      }
    }

    // Check for nearby player touches if the ball isn't currently phased
    if (ball->state == BallState::World && TICK_DIFF(tick, ball->last_touch_timestamp) >= pass_delay) {
      Vector2f position(ball->x / 16000.0f, ball->y / 16000.0f);

      float closest_distance = FLT_MAX;
      Player* closest_player = nullptr;

      // Loop over players to find anyone close enough to pick up the ball
      for (size_t j = 0; j < player_manager.player_count; ++j) {
        Player* player = player_manager.players + j;

        if (player->ship == 8) continue;
        if (player->enter_delay > 0.0f) continue;
        if (!player_manager.IsSynchronized(*player)) continue;
        if (player->id == ball->carrier_id && (ball->vel_x != 0 || ball->vel_y != 0)) continue;
        if (player->attach_parent != kInvalidPlayerId) continue;
        if (IsCarryingBall() && player->id == player_manager.player_id) continue;

        float pickup_radius = connection.settings.ShipSettings[player->ship].SoccerBallProximity / 16.0f;
        float dist_sq = position.DistanceSq(player->position);

        if (dist_sq <= pickup_radius * pickup_radius && dist_sq < closest_distance) {
          closest_distance = dist_sq;
          closest_player = player;
        }
      }

      if (closest_player) {
        if (closest_player->id == player_manager.player_id && TICK_DIFF(tick, last_pickup_request) >= 100) {
          // Send pickup
          connection.SendBallPickup((u8)ball->id, ball->timestamp);
          last_pickup_request = tick;
        }

        ball->last_touch_timestamp = GetCurrentTick();
      }
    }
  }

  anim_t += dt;

  if (anim_t >= Graphics::anim_powerball.duration) {
    anim_t -= Graphics::anim_powerball.duration;
  }
}

bool Soccer::FireBall(BallFireMethod method) {
  if (!IsCarryingBall()) return false;

  assert(carry_id < NULLSPACE_ARRAY_SIZE(balls));

  if (method == BallFireMethod::Gun && connection.settings.AllowGuns) {
    return false;
  } else if (method == BallFireMethod::Bomb && connection.settings.AllowBombs) {
    return false;
  }

  Player* self = player_manager.GetSelf();

  if (!self) return false;

  Powerball* ball = balls + carry_id;

  float speed = connection.settings.ShipSettings[self->ship].SoccerBallSpeed / 10.0f / 16.0f;
  Vector2f position = GetBallPosition(player_manager, *ball, GetMicrosecondTick());
  Vector2f heading = OrientationToHeading((u8)(self->orientation * 40.0f));
  Vector2f velocity = self->velocity + Vector2f(heading) * speed;

  u32 timestamp = MAKE_TICK(GetCurrentTick() + connection.time_diff);

  connection.SendBallFire((u8)carry_id, self->position, velocity, self->id, timestamp);
  carry_id = kInvalidBallId;

  player_manager.ship_controller->AddBombDelay(50);
  player_manager.ship_controller->AddBulletDelay(50);

  return true;
}

void Soccer::Simulate(Powerball& ball, bool drop_trail) {
  if (ball.friction <= 0) return;

  SimulateAxis(ball, connection.map, &ball.x, &ball.vel_x);
  SimulateAxis(ball, connection.map, &ball.y, &ball.vel_y);

  if (ball.state != BallState::Goal && ball.carrier_id == player_manager.player_id) {
    TileId tile_id = connection.map.GetTileId(ball.x / 16000, ball.y / 16000);

    if (tile_id == kGoalTileId) {
      Vector2f position(ball.x / 16000.0f, ball.y / 16000.0f);

      if (!IsTeamGoal(position)) {
        connection.SendBallGoal((u8)ball.id, GetCurrentTick() + connection.time_diff);
        ball.state = BallState::Goal;
      }
    }
  }

  // Drop trail if the ball is moving
  if (drop_trail && (ball.vel_x != 0 || ball.vel_y != 0)) {
    if (--ball.trail_delay <= 0) {
      Vector2f offset = Graphics::anim_powerball_trail.frames[0].dimensions * (0.5f / 16.0f);
      Vector2f position = Vector2f(ball.x / 16000.0f, ball.y / 16000.0f) - offset;

      AnimationSystem& anim_system = player_manager.weapon_manager->animation;
      Animation* anim = anim_system.AddAnimation(Graphics::anim_powerball_trail, position.PixelRounded());

      if (anim) {
        anim->layer = Layer::AfterWeapons;
      }

      ball.trail_delay = 5;
    }
  }

  s32 friction = ball.friction / 1000;
  ball.vel_x = (ball.vel_x * friction) / 1000;
  ball.vel_y = (ball.vel_y * friction) / 1000;

  ball.friction -= ball.friction_delta;

  ball.next_x = ball.x;
  ball.next_y = ball.y;

  SimulateAxis(ball, connection.map, &ball.next_x, &ball.vel_x);
  SimulateAxis(ball, connection.map, &ball.next_y, &ball.vel_y);
}

void Soccer::Clear() {
  memset(balls, 0, sizeof(balls));

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    ball->id = kInvalidBallId;
    ball->carrier_id = kInvalidPlayerId;
    ball->timestamp = 0;
    ball->last_touch_timestamp = GetCurrentTick();
  }

  anim_t = 0.0f;
  last_pickup_request = GetCurrentTick();
}

void Soccer::OnPowerballPosition(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // Type

  u8 ball_id = buffer.ReadU8();
  u16 x = buffer.ReadU16();
  u16 y = buffer.ReadU16();
  s16 velocity_x = buffer.ReadU16();
  s16 velocity_y = buffer.ReadU16();
  u16 owner_id = buffer.ReadU16();
  u32 timestamp = buffer.ReadU32() & 0x7FFFFFFF;

  if (ball_id >= NULLSPACE_ARRAY_SIZE(balls)) return;

  Powerball* ball = balls + ball_id;

  bool new_ball_pos_pkt = ball->id == kInvalidBallId || TICK_GT(timestamp, ball->timestamp) ||
                          ball->state == BallState::Goal ||
                          (ball->state == BallState::Carried && timestamp != 0);
  ball->id = ball_id;

  if (new_ball_pos_pkt) {
    ball->x = x * 1000;
    ball->y = y * 1000;
    ball->next_x = ball->x;
    ball->next_y = ball->y;
    ball->vel_x = velocity_x;
    ball->vel_y = velocity_y;
    ball->frequency = 0xFFFF;
    ball->state = BallState::World;

    if (ball_id == carry_id) {
      carry_id = kInvalidBallId;
    }

    u32 current_timestamp = MAKE_TICK(GetCurrentTick() + connection.time_diff);
    s32 sim_ticks = TICK_DIFF(current_timestamp, timestamp);

    if (sim_ticks > 6000 || sim_ticks < 0) {
      sim_ticks = 6000;
    }

    if (timestamp == 0) {
      sim_ticks = 0;
    }

    // The way this is setup seems like it could desynchronize state depending on brick setup at the time.
    // Maybe initial synchronization should ignore current bricks?
    //
    // This also seems to send owner id of 0xFFFF so how can a new client join and fully synchronize without knowing
    // the ball friction? Each ship has its own friction but the ship can't be determined based on the data sent.
    //
    // I don't think there's a way to actually solve this. Does Continuum also not synchronize correctly in certain
    // situations?

    u8 ship = 0;

    if (owner_id != kInvalidPlayerId) {
      Player* carrier = player_manager.GetPlayerById(owner_id);

      if (carrier) {
        carrier->ball_carrier = false;
        ball->frequency = carrier->frequency;

        ship = carrier->ship;

        if (ship == 8) {
          ship = 0;
        }
      }

      if (owner_id == player_manager.player_id) {
        last_pickup_request = GetCurrentTick();
      }

      ball->last_touch_timestamp = GetCurrentTick();
    }

    if (ball->vel_x != 0 || ball->vel_y != 0) {
      ball->friction_delta = connection.settings.ShipSettings[ship].SoccerBallFriction;
      ball->friction = 1000000;
    } else {
      ball->friction = 0;
    }

    ball->carrier_id = owner_id;

    for (s32 i = 0; i < sim_ticks; ++i) {
      Simulate(*ball, false);
    }

    ball->last_micro_tick = GetMicrosecondTick();

    ball->timestamp = timestamp;
  } else if (timestamp == 0) {
    // Ball is carried if the timestamp is zero.
    ball->timestamp = timestamp;
    ball->carrier_id = owner_id;
    ball->vel_x = ball->vel_y = 0;
    ball->last_micro_tick = GetMicrosecondTick();

    Player* carrier = player_manager.GetPlayerById(owner_id);

    if (ball->state != BallState::Carried && carrier && carrier->ship != 8) {
      ball->state = BallState::Carried;
      carrier->ball_carrier = true;

      if (carrier->id == player_manager.player_id) {
        ShipSettings& ship_settings = connection.settings.ShipSettings[carrier->ship];

        this->carry_timer = ship_settings.SoccerBallThrowTimer / 100.0f;
        this->carry_id = ball->id;

        player_manager.ship_controller->AddBombDelay(ship_settings.BombFireDelay);
      }
    }
  }
}

bool OnMode3(const Vector2f& position, u32 frequency) {
  u32 corner = frequency % 4;

  switch (corner) {
    case 0: {
      return position.x < 512 && position.y < 512;
    } break;
    case 1: {
      return position.x >= 512 && position.y < 512;
    } break;
    case 2: {
      return position.x < 512 && position.y >= 512;
    } break;
    case 3: {
      return position.x >= 512 && position.y >= 512;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool OnMode5(const Vector2f& position, u32 frequency) {
  u32 direction = frequency % 4;

  switch (direction) {
    case 0: {
      if (position.y < 512) {
        return position.x < position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 1: {
      if (position.x < 512) {
        return position.x + position.y >= 1024;
      }

      return position.x < position.y;
    } break;
    case 2: {
      if (position.x < 512) {
        return position.x >= position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 3: {
      if (position.y <= 512) {
        return position.x + position.y >= 1024;
      }

      return position.x >= position.y;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool Soccer::IsTeamGoal(const Vector2f& position) {
  u32 frequency = player_manager.specview->GetFrequency();

  switch (connection.settings.SoccerMode) {
    case 0: {
      return false;
    } break;
    case 1: {
      if (frequency & 1) {
        return position.x >= 512;
      }

      return position.x < 512;
    } break;
    case 2: {
      if (frequency & 1) {
        return position.y >= 512;
      }

      return position.y < 512;
    } break;
    case 3: {
      return OnMode3(position, frequency);
    } break;
    case 4: {
      return !OnMode3(position, frequency);
    } break;
    case 5: {
      return OnMode5(position, frequency);
    } break;
    case 6: {
      return !OnMode5(position, frequency);
    }
    default: {
    } break;
  }

  return true;
}

}  // namespace null
