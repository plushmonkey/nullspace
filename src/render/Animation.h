#ifndef NULLSPACE_RENDER_ANIMATION_H_
#define NULLSPACE_RENDER_ANIMATION_H_

#include "../Math.h"
#include "Layer.h"
#include "Sprite.h"

namespace null {

struct AnimatedSprite {
  SpriteRenderable* frames;
  size_t frame_count;
  float duration;
};

struct Animation {
  AnimatedSprite* sprite = nullptr;
  Vector2f position = Vector2f(0, 0);
  Layer layer = Layer::Weapons;
  u16 id;
  float t = 0.0f;
  bool repeat = false;

  bool IsAnimating() { return sprite && t < sprite->duration; }

  SpriteRenderable& GetFrame() {
    size_t frame = (size_t)((t / sprite->duration) * sprite->frame_count);
    return sprite->frames[frame];
  }
};

struct Camera;
struct SpriteRenderer;

struct AnimationSystem {
  u16 next_id = 0;
  size_t animation_count;
  Animation animations[65535];

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);

  Animation* AddAnimation(AnimatedSprite& sprite, const Vector2f& position);
};

}  // namespace null

#endif
