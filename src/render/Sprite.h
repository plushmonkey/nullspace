#ifndef NULLSPACE_RENDER_SPRITE_H_
#define NULLSPACE_RENDER_SPRITE_H_

#include "../Math.h"

namespace null {

struct SpriteRenderable {
  Vector2f uvs[4];
  Vector2f dimensions;
  unsigned int texture = -1;
};

}  // namespace null

#endif
