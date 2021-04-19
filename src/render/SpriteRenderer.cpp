#include "SpriteRenderer.h"

#include <cassert>
#include <cstdio>

#include "../Math.h"
#include "Camera.h"
#include "Graphics.h"
#include "Image.h"

namespace null {

struct SpriteVertex {
  Vector2f position;
  Vector2f uv;

  SpriteVertex(const Vector2f& position, const Vector2f& uv) : position(position), uv(uv) {}
};

struct SpritePushElement {
  SpriteVertex vertices[6];
};

constexpr size_t kPushBufferSize = Megabytes(16);
constexpr size_t kTextureBufferSize = kPushBufferSize / sizeof(SpriteVertex);

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

  if (color.r == 0.0 && color.g == 0.0 && color.b == 0.0) {
    discard;
  }
}
)";

bool SpriteRenderer::Initialize(MemoryArena& perm_arena) {
  this->push_buffer = perm_arena.CreateArena(kPushBufferSize);
  this->texture_push_buffer = perm_arena.CreateArena(kTextureBufferSize);

  if (!shader.Initialize(kSpriteVertexShaderCode, kSpriteFragmentShaderCode)) {
    fprintf(stderr, "Failed to create sprite shader.\n");
    return false;
  }

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  GLsizeiptr max_pushes = push_buffer.max_size / sizeof(SpritePushElement);
  GLsizeiptr vbo_size = max_pushes * sizeof(SpriteVertex) * 6;

  glBufferData(GL_ARRAY_BUFFER, vbo_size, nullptr, GL_DYNAMIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, uv));
  glEnableVertexAttribArray(1);

  mvp_uniform = glGetUniformLocation(shader.program, "mvp");
  color_uniform = glGetUniformLocation(shader.program, "color_sampler");

  shader.Use();
  glUniform1i(color_uniform, 0);

  return true;
}

SpriteRenderable* SpriteRenderer::LoadSheet(const char* filename, const Vector2f& dimensions, int* count) {
  int width, height;

  u8* image = ImageLoad(filename, &width, &height);

  *count = 0;

  if (!image) {
    return nullptr;
  }

  size_t texture_index = texture_count++;
  GLuint* texture_id = textures + texture_index;

  glGenTextures(1, texture_id);
  glBindTexture(GL_TEXTURE_2D, *texture_id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

  ImageFree(image);

  SpriteRenderable* result = renderables + renderable_count;

  for (int top = 0; top < height; top += (int)dimensions.y) {
    for (int left = 0; left < width; left += (int)dimensions.x) {
      SpriteRenderable* renderable = renderables + renderable_count++;
      int bottom = top + (int)dimensions.y;
      int right = left + (int)dimensions.x;

      renderable->texture = *texture_id;
      renderable->dimensions = dimensions;
      renderable->uvs[0] = Vector2f(left / (float)width, top / (float)height);
      renderable->uvs[1] = Vector2f(right / (float)width, top / (float)height);
      renderable->uvs[2] = Vector2f(left / (float)width, bottom / (float)height);
      renderable->uvs[3] = Vector2f(right / (float)width, bottom / (float)height);
      ++*count;
    }
  }

  return result;
}

void SpriteRenderer::DrawText(Camera& camera, const char* text, TextColor color, const Vector2f& position,
                              TextAlignment alignment) {
  constexpr size_t kCountPerColor = 96;
  constexpr size_t kForeignCountPerColor = 24 * 3;

  Vector2f current_pos = position;
  size_t length = strlen(text);
  float start_x = current_pos.x;

  if (alignment == TextAlignment::Center) {
    start_x -= (length * 8.0f) / 2.0f;
  } else if (alignment == TextAlignment::Right) {
    start_x -= (length * 8.0f);
  }

  current_pos.x = start_x;

  u8 c;
  while (c = *text++) {
    if (c == '\n') {
      current_pos.x = start_x;
      current_pos.y += 12.0f;
      continue;
    }

    SpriteRenderable* base_renderable = Graphics::character_set[c];

    if (base_renderable) {
      SpriteRenderable* renderable = base_renderable;

      if (base_renderable >= Graphics::textf_sprites) {
        size_t index = (base_renderable - Graphics::textf_sprites);
        renderable = Graphics::textf_sprites + index + kForeignCountPerColor * (size_t)color;
      } else {
        size_t index = (base_renderable - Graphics::text_sprites);
        renderable = Graphics::text_sprites + index + kCountPerColor * (size_t)color;
      }

      Draw(camera, *renderable, current_pos);
    }

    current_pos += Vector2f(8.0f * camera.scale, 0);
  }
}

void SpriteRenderer::Draw(Camera& camera, const SpriteRenderable& renderable, const Vector2f& position) {
  SpritePushElement* element = memory_arena_push_type(&push_buffer, SpritePushElement);
  GLuint* texture_storage = memory_arena_push_type(&texture_push_buffer, GLuint);

  *texture_storage = renderable.texture;

  Vector2f dimensions = renderable.dimensions * camera.scale;

  element->vertices[0].position = position;
  element->vertices[0].uv = renderable.uvs[0];

  element->vertices[1].position = position + Vector2f(0, dimensions.y);
  element->vertices[1].uv = renderable.uvs[2];

  element->vertices[2].position = position + Vector2f(dimensions.x, 0);
  element->vertices[2].uv = renderable.uvs[1];

  element->vertices[3].position = position + Vector2f(dimensions.x, 0);
  element->vertices[3].uv = renderable.uvs[1];

  element->vertices[4].position = position + Vector2f(0, dimensions.y);
  element->vertices[4].uv = renderable.uvs[2];

  element->vertices[5].position = position + Vector2f(dimensions.x, dimensions.y);
  element->vertices[5].uv = renderable.uvs[3];
}

void SpriteRenderer::Render(Camera& camera) {
  shader.Use();
  glBindVertexArray(vao);

  glActiveTexture(GL_TEXTURE0);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  mat4 proj = camera.GetProjection();
  mat4 view = camera.GetView();
  mat4 mvp = proj * view;

  glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, (const GLfloat*)mvp.data);

  SpritePushElement* element = (SpritePushElement*)push_buffer.base;
  GLuint* texture_ptr = (GLuint*)texture_push_buffer.base;

  GLuint current_texture = *texture_ptr;
  GLsizei vertex_count = 0;
  glBindTexture(GL_TEXTURE_2D, current_texture);

  void* draw_base = push_buffer.base;

  while (element < (SpritePushElement*)push_buffer.current) {
    GLuint element_texture = *texture_ptr;

    if (element_texture != current_texture) {
      glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(SpriteVertex), draw_base);
      glDrawArrays(GL_TRIANGLES, 0, vertex_count);
      glBindTexture(GL_TEXTURE_2D, element_texture);

      current_texture = element_texture;
      draw_base = element;
      vertex_count = 0;
    }

    vertex_count += 6;
    ++element;
    ++texture_ptr;
  }

  if (vertex_count > 0) {
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(SpriteVertex), draw_base);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
  }

  push_buffer.Reset();
  texture_push_buffer.Reset();
}

}  // namespace null
