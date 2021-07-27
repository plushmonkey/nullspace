#ifndef NULLSPACE_SOCCER_H_
#define NULLSPACE_SOCCER_H_

#include "Clock.h"
#include "Math.h"
#include "Types.h"
#include "render/Animation.h"

namespace null {

struct Camera;
struct Connection;
struct PlayerManager;
struct SpectateView;
struct SpriteRenderer;
struct Vector2f;

struct Powerball {
  u16 id;
  u16 carrier_id;

  u16 frequency;
  s16 friction_delta;

  u32 friction;

  u32 x;
  u32 y;

  s16 vel_x;
  s16 vel_y;

  u32 next_x;
  u32 next_y;

  u32 timestamp;
  u64 last_micro_tick;

  u32 last_touch_timestamp;
  s32 trail_delay;

  bool in_goal;
};

constexpr u16 kInvalidBallId = 0xFFFF;

enum class BallFireMethod { Gun, Bomb, Warp };

struct Soccer {
  PlayerManager& player_manager;
  Connection& connection;

  float anim_t = 0.0f;
  u32 last_pickup_request = 0;
  float carry_timer = 0.0f;
  u16 carry_id = kInvalidBallId;

  Powerball balls[8];

  Soccer(PlayerManager& player_manager);

  void Render(Camera& camera, SpriteRenderer& renderer);
  void RenderIndicator(Powerball& ball, const Vector2f& position);

  void Update(float dt);
  void Simulate(Powerball& ball, bool drop_trail);
  bool FireBall(BallFireMethod method);

  void Clear();

  void OnPowerballPosition(u8* pkt, size_t size);

  bool IsTeamGoal(const Vector2f& position);

  inline bool IsCarryingBall() { return carry_id != kInvalidBallId; }
};

}  // namespace null

#endif
