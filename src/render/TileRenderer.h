#ifndef NULLSPACE_RENDER_TILERENDERER_H_
#define NULLSPACE_RENDER_TILERENDERER_H_

#include "Shader.h"

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

  bool Initialize();
  void Render(Camera& camera);
  bool CreateMapBuffer(MemoryArena& temp_arena, const char* filename);
};

}  // namespace null

#endif
