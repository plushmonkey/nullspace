#ifndef NULLSPACE_RENDER_SHADER_H_
#define NULLSPACE_RENDER_SHADER_H_

#include <glad/glad.h>

namespace null {

#ifdef __ANDROID__
#define NULL_SHADER_VERSION "#version 300 es"
#else
#define NULL_SHADER_VERSION "#version 150"
#endif

struct ShaderProgram {
  bool Initialize(const char* vertex_code, const char* fragment_code);
  void Use();

  void Cleanup();

  GLuint program;
};

}  // namespace null

#endif
