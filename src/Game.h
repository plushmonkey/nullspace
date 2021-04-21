#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

#include "ChatController.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "SpectateView.h"
#include "StatBox.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/AnimatedTileRenderer.h"
#include "render/Animation.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"
#include "render/TileRenderer.h"

namespace null {

struct MemoryArena;

struct Game {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  AnimationSystem animation;
  PacketDispatcher dispatcher;
  Connection connection;
  PlayerManager player_manager;
  WeaponManager weapon_manager;
  Camera camera;
  Camera ui_camera;
  TileRenderer tile_renderer;
  AnimatedTileRenderer animated_tile_renderer;
  SpriteRenderer sprite_renderer;
  StatBox statbox;
  ChatController chat;
  SpectateView specview;
  float fps;
  bool render_radar = false;
  bool menu_open = false;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height);

  bool Initialize(InputState& input);
  void Update(const InputState& input, float dt);

  void Render(float dt);
  void RenderRadar(Player* player);
  void RenderMenu();
  void HandleMenuKey(int codepoint, int mods);
};

}  // namespace null

#endif
