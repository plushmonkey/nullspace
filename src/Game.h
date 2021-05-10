#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

#include "ChatController.h"
#include "InputState.h"
#include "LvzController.h"
#include "Memory.h"
#include "PlayerManager.h"
#include "ShipController.h"
#include "SpectateView.h"
#include "StatBox.h"
#include "WeaponManager.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/AnimatedTileRenderer.h"
#include "render/Animation.h"
#include "render/BackgroundRenderer.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"
#include "render/TileRenderer.h"

namespace null {

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
  BackgroundRenderer background_renderer;
  StatBox statbox;
  ChatController chat;
  SpectateView specview;
  ShipController ship_controller;
  LvzController lvz;
  float fps;
  bool render_radar = false;
  bool menu_open = false;
  bool menu_quit = false;
  int mapzoom = 0;

  size_t flag_count = 0;
  GameFlag flags[256];

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height);

  bool Initialize(InputState& input);
  void Cleanup();

  bool Update(const InputState& input, float dt);

  void Render(float dt);

  void RenderGame(float dt);
  void RenderJoin(float dt);

  void RenderRadar(Player* player);
  void RenderMenu();
  bool HandleMenuKey(int codepoint, int mods);

  void RecreateRadar();

  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagPosition(u8* pkt, size_t size);
  void OnPlayerId(u8* pkt, size_t size);
};

}  // namespace null

#endif
