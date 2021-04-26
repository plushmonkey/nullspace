#ifndef NULLSPACE_RENDER_ANIMATEDTILERENDERER_H_
#define NULLSPACE_RENDER_ANIMATEDTILERENDERER_H_

#include "Animation.h"

namespace null {

struct Camera;
struct Map;
struct SpriteRenderer;
struct Vector2f;

struct AnimatedTileRenderer {
  // Store these here instead of in the animation system to keep them all in sync
  Animation anim_flag;
  Animation anim_flag_team;
  Animation anim_goal;
  Animation anim_goal_team;
  Animation anim_asteroid_small1;
  Animation anim_asteroid_small2;
  Animation anim_asteroid_large;
  Animation anim_space_station;
  Animation anim_wormhole;

  bool Initialize();
  void Update(float dt);

  // TODO: Pass in game flags to render
  void Render(SpriteRenderer& renderer, Map& map, Camera& camera, const Vector2f& screen_dim, struct GameFlag* flags,
              size_t flag_count, u32 freq);
};

}  // namespace null

#endif
