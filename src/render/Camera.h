#ifndef NULLSPACE_RENDER_CAMERA_H_
#define NULLSPACE_RENDER_CAMERA_H_

#include "../Math.h"

namespace null {

struct Camera {
  Vector2f position = Vector2f(512, 512);
  float surface_width;
  float surface_height;
  float zoom = 1.0f / 16.0f;

  Camera(float surface_width, float surface_height) : surface_width(surface_width), surface_height(surface_height) {}

  mat4 GetView() { return Translate(mat4::Identity(), Vector3f(-position.x, -position.y, 0.0f)); }
  mat4 GetProjection() {
    return Orthographic(-surface_width / 2.0f * zoom, surface_width / 2.0f * zoom, surface_height / 2.0f * zoom,
                        -surface_height / 2.0f * zoom, -1.0f, 1.0f);
  }
};

}  // namespace null

#endif
