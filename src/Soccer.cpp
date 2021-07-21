#include "Soccer.h"

#include <stdio.h>
#include <string.h>

#include "Clock.h"
#include "PlayerManager.h"
#include "Radar.h"
#include "SpectateView.h"
#include "net/Connection.h"

namespace null {

constexpr u16 kInvalidBallId = 0xFFFF;

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

Soccer::Soccer(PlayerManager& player_manager) : player_manager(player_manager), connection(player_manager.connection) {
  connection.dispatcher.Register(ProtocolS2C::PowerballPosition, OnPowerballPositionPkt, this);

  Clear();
}

void Soccer::Render(Camera& camera, SpriteRenderer& renderer) {
  Animation animation;

  animation.t = anim_t;

  u64 microtick = GetMicrosecondTick();

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    if (ball->id != kInvalidBallId) {
      // TODO: Select sprite based on ball state
      animation.sprite = &Graphics::anim_powerball;

      SpriteRenderable& renderable = animation.GetFrame();
      Vector2f current_position(ball->x / 16000.0f, ball->y / 16000.0f);
      Vector2f next_position(ball->next_x / 16000.0f, ball->next_y / 16000.0f);
      float t = (microtick - ball->last_micro_tick) / (float)kTickDurationMicro;
      Vector2f position = current_position * (1 - t) + (t * next_position);
      Vector2f render_position = position - renderable.dimensions * (0.5f / 16.0f);

      renderer.Draw(camera, renderable, render_position, Layer::AfterWeapons);
      player_manager.radar->AddTemporaryIndicator(position, 0, Vector2f(2, 2), ColorType::RadarTeamFlag);
    }
  }
}

void Soccer::Update(float dt) {
  // TODO: Every tick drop an animation for the trail if the ball is moving
  u64 microtick = GetMicrosecondTick();

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    if (ball->id == kInvalidBallId) continue;

    while ((s64)(microtick - ball->last_micro_tick) >= kTickDurationMicro) {
      Simulate(*ball);
      ball->last_micro_tick += kTickDurationMicro;
    }
  }

  anim_t += dt;

  if (anim_t >= Graphics::anim_powerball.duration) {
    anim_t -= Graphics::anim_powerball.duration;
  }
}

void Soccer::Simulate(Powerball& ball) {
  if (ball.friction <= 0) return;

  SimulateAxis(ball, connection.map, &ball.x, &ball.vel_x);
  SimulateAxis(ball, connection.map, &ball.y, &ball.vel_y);

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
  }

  anim_t = 0.0f;
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
  u32 timestamp = buffer.ReadU32();

  if (ball_id >= NULLSPACE_ARRAY_SIZE(balls)) return;

  Powerball* ball = balls + ball_id;

  ball->id = ball_id;

  if (TICK_GT(timestamp, ball->timestamp)) {
    ball->x = x * 1000;
    ball->y = y * 1000;
    ball->vel_x = velocity_x;
    ball->vel_y = velocity_y;

    u32 current_timestamp = GetCurrentTick() + connection.time_diff;
    s32 sim_ticks = TICK_DIFF(current_timestamp, timestamp);

    if (sim_ticks > 6000) {
      sim_ticks = 6000;
    }

    if (timestamp == 0) {
      sim_ticks = 0;
    }

    // TODO: Figure out when to set different things for the ball to determine its state.

    if (owner_id != kInvalidPlayerId) {
      Player* carrier = player_manager.GetPlayerById(owner_id);

      if (carrier) {
        ball->frequency = carrier->frequency;
        u8 ship = carrier->ship;
        if (ship == 8) ship = 1;

        ball->friction_delta = connection.settings.ShipSettings[ship].SoccerBallFriction;
        ball->friction = 1000000;
      }
    }

    ball->carrier_id = owner_id;

    for (s32 i = 0; i < sim_ticks; ++i) {
      Simulate(*ball);
    }

    ball->last_micro_tick = GetMicrosecondTick();

    ball->timestamp = timestamp;
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
