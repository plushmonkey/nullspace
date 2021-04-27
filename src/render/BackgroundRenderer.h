#ifndef NULLSPACE_RENDER_BACKGROUNDRENDERER_H_
#define NULLSPACE_RENDER_BACKGROUNDRENDERER_H_

#include "../Math.h"
#include "../Types.h"
#include "Shader.h"
#include "Sprite.h"

namespace null {

struct Camera;
struct MemoryArena;
struct SpriteRenderer;

struct BackgroundRenderer {
  // Set of textures with stars in them that get transformed for the background
  GLuint textures[8];

  SpriteRenderable* renderables = nullptr;

  bool Initialize(MemoryArena& perm_arena, MemoryArena& temp_arena, const Vector2f& surface_dim);
  void Render(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim);

private:
  void RenderParallaxLayer(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim, float speed, int layer);
  void GenerateTextures(MemoryArena& temp_arena, GLuint* textures, size_t texture_count, u8 color);
};

}  // namespace null

#endif
