#include "Animation.h"

#include <null/render/Camera.h>
#include <null/render/SpriteRenderer.h>

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
        animations[i--] = animations[--animation_count];
      }
    }
  }
}

void AnimationSystem::Render(Camera& camera, SpriteRenderer& renderer) {
  for (size_t i = 0; i < animation_count; ++i) {
    Animation* animation = animations + i;
    SpriteRenderable& frame = animation->GetFrame();

    // Offset the layer by the id for consistent ordering when swapping in list
    float z = (float)animation->layer + (animation->id / 65535.0f);

    renderer.Draw(camera, frame, Vector3f(animation->position.x, animation->position.y, z));
  }
}

Animation* AnimationSystem::AddAnimation(AnimatedSprite& sprite, const Vector2f& position) {
  Animation* animation = animations + animation_count++;

  animation->sprite = &sprite;
  animation->t = 0.0f;
  animation->position = position;
  animation->id = next_id++;

  // Manually roll around so 65535 is never used.
  // If 65535 could be used then the z calculation would be 1.0f causing it to render one layer too high.
  if (next_id == 65535) {
    next_id = 0;
  }

  return animation;
}

}  // namespace null
