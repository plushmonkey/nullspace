#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

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
  TileRenderer tile_renderer;
  SpriteRenderer sprite_renderer;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height);

  bool Initialize();
  void Update(float dt);
  void Render();
};

}  // namespace null

#endif
