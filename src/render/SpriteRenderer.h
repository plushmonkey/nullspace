#ifndef NULLSPACE_RENDER_SPRITERENDERER_H_
#define NULLSPACE_RENDER_SPRITERENDERER_H_

#include "Shader.h"

namespace null {

struct Camera;

struct SpriteRenderable {
  GLuint texture;
};

// TODO: Probably use a push buffer of renderables then render them all when render is called.
// TODO: Should there be async lazy texture loading? - No for now. Textures will need to be reloaded later for lvz
struct SpriteRenderer {
  ShaderProgram shader;

  GLint color_uniform = -1;
  GLint mvp_uniform = -1;

  GLuint vao = -1;
  GLuint vbo = -1;

  bool Initialize();
  void Render(Camera& camera);
};

}  // namespace null

#endif
