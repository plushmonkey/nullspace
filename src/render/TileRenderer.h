#ifndef NULLSPACE_RENDER_TILERENDERER_H_
#define NULLSPACE_RENDER_TILERENDERER_H_

#include "Animation.h"
#include "Shader.h"
#include "Sprite.h"

namespace null {

struct Camera;
struct Map;
struct MemoryArena;

struct TileRenderer {
  ShaderProgram shader;

  GLuint tilemap_texture = -1;
  GLuint tiledata_texture = -1;

  GLint tilemap_uniform = -1;
  GLint tiledata_uniform = -1;
  GLint mvp_uniform = -1;

  GLuint vao = -1;
  GLuint vbo = -1;

  // This is the radar that is used when pressing show radar key.
  GLuint radar_texture = -1;
  SpriteRenderable radar_renderable;

  // This is the fully rendered radar that is used for the small radar view
  GLuint full_radar_texture = -1;
  SpriteRenderable full_radar_renderable;

  bool Initialize();
  void Render(Camera& camera);
  bool CreateMapBuffer(MemoryArena& temp_arena, const char* filename, const Vector2f& surface_dim);
  bool CreateRadar(MemoryArena& temp_arena, const char* filename, const Vector2f& surface_dim, u16 mapzoom);

 private:
  void RenderRadar(Map& map, MemoryArena& temp_arena, u32 dimensions, SpriteRenderable& renderable, GLuint* texture, GLint filter);
};

}  // namespace null

#endif
