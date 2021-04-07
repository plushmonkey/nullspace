#ifndef NULLSPACE_RENDER_H_
#define NULLSPACE_RENDER_H_

#include <glad/glad.h>

#include "Math.h"

struct GLFWwindow;

namespace null {

struct Camera {
  Vector2f position = Vector2f(512, 512);
  float surface_width;
  float surface_height;
  float zoom = 1.0f;

  Camera(float surface_width, float surface_height) : surface_width(surface_width), surface_height(surface_height) {}

  mat4 GetView() { return Translate(mat4::Identity(), Vector3f(-position.x, -position.y, 0.0f)); }
  mat4 GetProjection() {
    return Orthographic(-surface_width / 2.0f * zoom, surface_width / 2.0f * zoom, surface_height / 2.0f * zoom,
                        -surface_height / 2.0f * zoom, -1.0f, 1.0f);
  }
};

struct ShaderProgram {
  bool Initialize(const char* vertex_code, const char* fragment_code);

  GLuint program;
};

struct MemoryArena;

struct RenderState {
  Camera camera;
  GLFWwindow* window = nullptr;

  ShaderProgram tile_shader;
  GLuint tilemap_texture = -1;
  GLuint tiledata_texture = -1;

  GLint tilemap_uniform = -1;
  GLint tiledata_uniform = -1;
  GLint mvp_uniform = -1;

  GLuint vao = -1;
  GLuint grid_vbo = -1;

  RenderState();

  bool Initialize(int width, int height);
  bool Render(float dt);

  bool CreateMapBuffer(MemoryArena& arena, const char* filename);
};

}  // namespace null

#endif
