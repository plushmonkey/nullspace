#include "SpriteRenderer.h"

#include "../Math.h"
#include "Camera.h"

namespace null {

struct SpriteVertex {
  Vector2f position;
  Vector2f uv;

  SpriteVertex(const Vector2f& position, const Vector2f& uv) : position(position), uv(uv) {}
};

const SpriteVertex kSpriteVertices[] = {
    SpriteVertex(Vector2f(0, 0), Vector2f(0, 0)), SpriteVertex(Vector2f(0, 1), Vector2f(0, 1)),
    SpriteVertex(Vector2f(1, 0), Vector2f(1, 0)), SpriteVertex(Vector2f(1, 0), Vector2f(1, 0)),
    SpriteVertex(Vector2f(0, 1), Vector2f(0, 1)), SpriteVertex(Vector2f(1, 1), Vector2f(1, 1)),
};

const char* kSpriteVertexShaderCode = R"(
#version 330

layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;

uniform mat4 mvp;

out vec2 varying_uv;

void main() {
  gl_Position = mvp * vec4(position, 0.0, 1.0);
  varying_uv = uv;
}
)";

const char* kSpriteFragmentShaderCode = R"(
#version 330

in vec2 varying_uv;

uniform sampler2D color_sampler;

out vec4 color;

void main() {
  color = texture(color_sampler, varying_uv);
}
)";

bool SpriteRenderer::Initialize() {
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kSpriteVertices), kSpriteVertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, uv));
  glEnableVertexAttribArray(1);

  mvp_uniform = glGetUniformLocation(shader.program, "mvp");
  color_uniform = glGetUniformLocation(shader.program, "color_sampler");

  glUniform1i(color_uniform, 0);

  return true;
}

void SpriteRenderer::Render(Camera& camera) {
#if 0  // TODO: Implement
  shader.Use();
  glBindVertexArray(vao);

  glActiveTexture(GL_TEXTURE0);

  mat4 proj = camera.GetProjection();
  mat4 view = camera.GetView();
  mat4 mvp = proj * view;

  glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, (const GLfloat*)mvp.data);

  glDrawArrays(GL_TRIANGLES, 0, 6);
#endif
}

}  // namespace null
