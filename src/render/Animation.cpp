#include "Animation.h"

#include "Camera.h"
#include "SpriteRenderer.h"

namespace null {

void AnimationSystem::Update(float dt) {
  for (size_t i = 0; i < animation_count; ++i) {
    Animation* animation = animations + i;

    animation->t += dt;

    if (!animation->IsAnimating()) {
      if (animation->repeat) {
        animation->t -= animation->sprite->duration;
      } else {
        // Remove animation by swapping with last one
        // TODO: This might be bad in cases where sprites overlap in game.
        // Could lead to inconsistencies by rendering in a different order.
        animations[i--] = animations[--animation_count];
      }
    }
  }
}

void AnimationSystem::Render(Camera& camera, SpriteRenderer& renderer) {
  for (size_t i = 0; i < animation_count; ++i) {
    Animation* animation = animations + i;
    SpriteRenderable& frame = animation->GetFrame();

    renderer.Draw(camera, frame, animation->position);
  }
}

Animation* AnimationSystem::AddAnimation(AnimatedSprite& sprite, const Vector2f& position) {
  Animation* animation = animations + animation_count++;

  animation->sprite = &sprite;
  animation->t = 0.0f;
  animation->position = position;

  return animation;
}

}  // namespace null
