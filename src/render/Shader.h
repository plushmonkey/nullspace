#ifndef NULLSPACE_RENDER_SHADER_H_
#define NULLSPACE_RENDER_SHADER_H_

#include <glad/glad.h>

namespace null {

struct ShaderProgram {
  bool Initialize(const char* vertex_code, const char* fragment_code);
  void Use();

  GLuint program;
};

}  // namespace null

#endif
