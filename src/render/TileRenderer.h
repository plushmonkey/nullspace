#ifndef NULLSPACE_RENDER_TILERENDERER_H_
#define NULLSPACE_RENDER_TILERENDERER_H_

#include "Shader.h"
#include "Sprite.h"

namespace null {

struct MemoryArena;
struct Camera;

struct TileRenderer {
  ShaderProgram shader;

  GLuint tilemap_texture = -1;
  GLuint tiledata_texture = -1;

  GLint tilemap_uniform = -1;
  GLint tiledata_uniform = -1;
  GLint mvp_uniform = -1;

  GLuint vao = -1;
  GLuint vbo = -1;

  GLuint radar_texture = -1;
  SpriteRenderable radar_renderable;

  bool Initialize();
  void Render(Camera& camera);
  bool CreateMapBuffer(MemoryArena& temp_arena, const char* filename, const Vector2f& surface_dim);

private:
  bool RenderRadar(MemoryArena& temp_arena, const char* filename, u32 surface_width, u32 surface_height);
};

}  // namespace null

#endif
