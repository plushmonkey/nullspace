#ifndef NULLSPACE_RENDER_CAMERA_H_
#define NULLSPACE_RENDER_CAMERA_H_

#include <null/Math.h>
#include <null/render/Layer.h>

namespace null {

struct Camera {
  Vector2f position;
  mat4 projection;
  Vector2f surface_dim;
  float scale;

  Camera(const Vector2f& surface_dim, const Vector2f& position, float scale)
      : surface_dim(surface_dim), position(position), scale(scale) {
    float zmax = (float)Layer::Count;
    projection = Orthographic(-surface_dim.x / 2.0f * scale, surface_dim.x / 2.0f * scale, surface_dim.y / 2.0f * scale,
                              -surface_dim.y / 2.0f * scale, -zmax, zmax);
  }

  mat4 GetView() { return Translate(mat4::Identity(), Vector3f(-position.x, -position.y, 0.0f)); }
  mat4 GetProjection() { return projection; }
};

}  // namespace null

#endif
