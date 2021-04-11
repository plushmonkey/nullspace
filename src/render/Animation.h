#ifndef NULLSPACE_RENDER_ANIMATION_H_
#define NULLSPACE_RENDER_ANIMATION_H_

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
  float t = 0.0f;

  bool IsAnimating() {
    return t < sprite->duration;
  }

  SpriteRenderable& GetFrame() {
    size_t frame = (size_t)((t / sprite->duration) * sprite->frame_count);
    return sprite->frames[frame];
  }
};

}  // namespace null

#endif
