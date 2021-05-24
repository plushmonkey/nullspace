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
  bool IsAnimating(float check_t) { return check_t < sprite->duration; }

  float GetDuration() { return sprite->duration; }

  inline SpriteRenderable& GetFrame() { return GetFrame(t); }

  SpriteRenderable& GetFrame(float user_t) {
    size_t frame = (size_t)((user_t / sprite->duration) * sprite->frame_count);
    if (frame >= sprite->frame_count) {
      frame = sprite->frame_count - 1;
    }
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
