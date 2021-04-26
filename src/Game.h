#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

#include "ChatController.h"
#include "InputState.h"
#include "LvzController.h"
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

struct GameFlag {
  u16 id = 0xFFFF;
  u16 owner = 0xFFFF;
  Vector2f position;
  bool dropped = false;
};

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
  LvzController lvz;
  float fps;
  bool render_radar = false;
  bool menu_open = false;

  size_t flag_count = 0;
  GameFlag flags[256];

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height);

  bool Initialize(InputState& input);
  void Update(const InputState& input, float dt);

  void Render(float dt);
  void RenderRadar(Player* player);
  void RenderMenu();
  void HandleMenuKey(int codepoint, int mods);

  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagPosition(u8* pkt, size_t size);
};

}  // namespace null

#endif
