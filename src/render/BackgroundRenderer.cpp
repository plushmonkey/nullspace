#include "BackgroundRenderer.h"

#include <cstdlib>

#include "../Memory.h"
#include "Camera.h"
#include "SpriteRenderer.h"

namespace null {

constexpr size_t kTextureDim = 1024;
constexpr int kStarsPerTexture = 100;

bool BackgroundRenderer::Initialize(MemoryArena& perm_arena, MemoryArena& temp_arena, const Vector2f& surface_dim) {
  static_assert(NULLSPACE_ARRAY_SIZE(textures) % 2 == 0, "Background texture array size must be even.");
  glGenTextures(NULLSPACE_ARRAY_SIZE(textures), textures);

  GenerateTextures(temp_arena, textures, 4, 0xB8);
  GenerateTextures(temp_arena, textures + 4, 4, 0x60);

  Vector2f viewable_dim = Vector2f(1024 * 16 + surface_dim.x, 1024 * 16 + surface_dim.y);
  size_t count_x = (size_t)ceil(viewable_dim.x / kTextureDim);
  size_t count_y = (size_t)ceil(viewable_dim.y / kTextureDim);
  size_t count_total = count_x * count_y;

  if (!renderables) {
    renderables = memory_arena_push_type_count(&perm_arena, SpriteRenderable, count_total * 2);
  }

  // Generate random uvs so the small texture patches can be flipped around randomly
  const Vector2f uvs[4][4] = {
      {Vector2f(0, 0), Vector2f(1, 0), Vector2f(0, 1), Vector2f(1, 1)},
      {Vector2f(1, 0), Vector2f(0, 0), Vector2f(0, 1), Vector2f(1, 1)},
      {Vector2f(0, 0), Vector2f(1, 0), Vector2f(1, 1), Vector2f(0, 1)},
      {Vector2f(1, 1), Vector2f(1, 0), Vector2f(0, 1), Vector2f(0, 0)},
  };

  for (size_t i = 0; i < count_total; ++i) {
    size_t texture_index = rand() % (NULLSPACE_ARRAY_SIZE(textures) / 2);
    renderables[i].texture = textures[texture_index];
    renderables[i].dimensions = Vector2f(kTextureDim, kTextureDim);

    size_t uv_index = rand() % 4;
    memcpy(renderables[i].uvs, uvs[uv_index], sizeof(Vector2f) * 4);
  }

  for (size_t i = count_total; i < count_total * 2; ++i) {
    size_t texture_index = rand() % (NULLSPACE_ARRAY_SIZE(textures) / 2);

    renderables[i].texture = textures[texture_index + NULLSPACE_ARRAY_SIZE(textures) / 2];
    renderables[i].dimensions = Vector2f(kTextureDim, kTextureDim);

    size_t uv_index = rand() % 4;
    memcpy(renderables[i].uvs, uvs[uv_index], sizeof(Vector2f) * 4);
  }

  return true;
}

void BackgroundRenderer::GenerateTextures(MemoryArena& temp_arena, GLuint* textures, size_t texture_count, u8 color) {
  for (size_t i = 0; i < texture_count; ++i) {
    glBindTexture(GL_TEXTURE_2D, textures[i]);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    ArenaSnapshot snapshot = temp_arena.GetSnapshot();
    u8* data = temp_arena.Allocate(kTextureDim * kTextureDim * 3);

    memset(data, 0, kTextureDim * kTextureDim * 3);
    for (int i = 0; i < kStarsPerTexture; ++i) {
      u32 x = rand() % kTextureDim;
      u32 y = rand() % kTextureDim;

      size_t index = (y * kTextureDim + x) * 3;
      data[index] = color;
      data[index + 1] = color;
      data[index + 2] = color;
    }

    // TODO: This could be stored in one channel with a custom shader. It would save a lot of gpu memory.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kTextureDim, kTextureDim, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    temp_arena.Revert(snapshot);
  }
}

void BackgroundRenderer::Render(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim) {
  RenderParallaxLayer(camera, renderer, surface_dim, 0.25f, 1);
  RenderParallaxLayer(camera, renderer, surface_dim, 0.50f, 0);
}

void BackgroundRenderer::RenderParallaxLayer(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim,
                                             float speed, int layer) {
  Vector2f viewable_dim = Vector2f(1024 * 16 + surface_dim.x, 1024 * 16 + surface_dim.y);
  // The renderables are offset so they can render the side areas outside of the map with easy index lookup
  Vector2f offset = surface_dim * (0.5f / 16.0f);
  Vector2f lookup = camera.position * 16.0f * speed;

  s32 count_x = (s32)ceil(viewable_dim.x / kTextureDim);
  s32 count_y = (s32)ceil(viewable_dim.y / kTextureDim);
  size_t count_total = count_x * count_y;

  // Calculate viewable textures
  s32 start_x = (s32)((lookup.x / viewable_dim.x) * count_x - 1);
  s32 start_y = (s32)((lookup.y / viewable_dim.y) * count_y - 1);
  if (start_x < 0) start_x = 0;
  if (start_y < 0) start_y = 0;

  s32 end_x = (s32)ceil(start_x + (surface_dim.x / kTextureDim) + 1);
  s32 end_y = (s32)ceil(start_y + (surface_dim.y / kTextureDim) + 1);
  if (end_x >= count_x) end_x = count_x - 1;
  if (end_y >= count_y) end_y = count_y - 1;

  for (s32 y = start_y; y <= end_y; ++y) {
    for (s32 x = start_x; x <= end_x; ++x) {
      size_t index = y * count_x + x;
      Vector2f position((x * kTextureDim) / 16.0f, (y * kTextureDim) / 16.0f);

      // Scale the position back out into view space
      position = position + camera.position * (1.0f - speed);

      renderer.Draw(camera, renderables[index + count_total * layer], position - offset, Layer::Background);
    }
  }
}

void BackgroundRenderer::Cleanup() { glDeleteTextures(NULLSPACE_ARRAY_SIZE(textures), textures); }

}  // namespace null
