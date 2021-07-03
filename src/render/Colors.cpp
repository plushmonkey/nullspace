#include "Colors.h"

namespace null {

constexpr float kAnimationCycle = 1.28f;

void Colors::Update(float dt) {
  animation_time += dt;

  while (animation_time >= kAnimationCycle) {
    animation_time -= kAnimationCycle;
  }
}

SpriteRenderable Colors::GetRenderable(ColorType type) {
  SpriteRenderable renderable;

  float left = (animation_time / kAnimationCycle);
  float right = left + (1.0f / kColorTextureWidth);
  float top = (float)type / kColorTextureHeight;
  float bottom = top + (1.0f / kColorTextureHeight);

  renderable.texture = texture_id;
  renderable.dimensions = Vector2f(1, 1);
  renderable.uvs[0] = Vector2f(left, top);
  renderable.uvs[1] = Vector2f(right, top);
  renderable.uvs[2] = Vector2f(left, bottom);
  renderable.uvs[3] = Vector2f(right, bottom);

  return renderable;
}

SpriteRenderable Colors::GetRenderable(ColorType type, const Vector2f& dimensions) {
  SpriteRenderable renderable = GetRenderable(type);

  renderable.dimensions = dimensions;

  return renderable;
}

}  // namespace null
