#ifndef NULLSPACE_SOCCER_H_
#define NULLSPACE_SOCCER_H_

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
};

struct Soccer {
  PlayerManager& player_manager;
  Connection& connection;

  float anim_t = 0.0f;

  Powerball balls[8];

  Soccer(PlayerManager& player_manager);

  void Render(Camera& camera, SpriteRenderer& renderer);

  void Update(float dt);
  void Simulate(Powerball& ball);

  void Clear();

  void OnPowerballPosition(u8* pkt, size_t size);

  bool IsTeamGoal(const Vector2f& position);
};

}  // namespace null

#endif
