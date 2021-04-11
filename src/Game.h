#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

#include "InputState.h"
#include "net/Connection.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"
#include "render/TileRenderer.h"

namespace null {

struct MemoryArena;

struct Game {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  Connection connection;
  Camera camera;
  Camera ui_camera;
  TileRenderer tile_renderer;
  SpriteRenderer sprite_renderer;
  float fps;

  SpriteRenderable* ship_sprites = nullptr;
  SpriteRenderable* spectate_sprites = nullptr;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height);

  bool Initialize();
  void Update(const InputState& input, float dt);
  void Render();
};

}  // namespace null

#endif
