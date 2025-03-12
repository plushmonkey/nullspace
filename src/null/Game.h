#ifndef NULLSPACE_GAME_H_
#define NULLSPACE_GAME_H_

#include <null/BannerPool.h>
#include <null/BrickManager.h>
#include <null/ChatController.h>
#include <null/InputState.h>
#include <null/LvzController.h>
#include <null/Memory.h>
#include <null/Notification.h>
#include <null/PlayerManager.h>
#include <null/Radar.h>
#include <null/Settings.h>
#include <null/ShipController.h>
#include <null/Soccer.h>
#include <null/Sound.h>
#include <null/SpectateView.h>
#include <null/StatBox.h>
#include <null/WeaponManager.h>
#include <null/net/Connection.h>
#include <null/net/PacketDispatcher.h>
#include <null/render/AnimatedTileRenderer.h>
#include <null/render/Animation.h>
#include <null/render/BackgroundRenderer.h>
#include <null/render/Camera.h>
#include <null/render/SpriteRenderer.h>
#include <null/render/TileRenderer.h>

namespace null {

enum {
  GameFlag_Dropped = (1 << 0),
  GameFlag_Turf = (1 << 1),
};

struct GameFlag {
  u16 id = 0xFFFF;
  u16 owner = 0xFFFF;
  u32 hidden_end_tick = 0;
  Vector2f position;

  u32 flags = 0;
  u32 last_pickup_request_tick = 0;
};
constexpr u32 kFlagPickupDelay = 20;

// This is the actual max green count in Continuum
constexpr size_t kMaxGreenCount = 256;
struct PrizeGreen {
  Vector2f position;
  u32 end_tick;
  s32 prize_id;
};

struct Game {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  WorkQueue& work_queue;
  SoundSystem sound_system;
  NotificationSystem notifications;
  AnimationSystem animation;
  PacketDispatcher dispatcher;
  Connection connection;
  PlayerManager player_manager;
  WeaponManager weapon_manager;
  BrickManager brick_manager;
  BannerPool banner_pool;
  Camera camera;
  Camera ui_camera;
  TileRenderer tile_renderer;
  AnimatedTileRenderer animated_tile_renderer;
  SpriteRenderer sprite_renderer;
  BackgroundRenderer background_renderer;
  StatBox statbox;
  ChatController chat;
  SpectateView specview;
  Soccer soccer;
  ShipController ship_controller;
  LvzController lvz;
  Radar radar;
  float fps;
  bool render_radar = false;
  bool menu_open = false;
  bool menu_quit = false;
  int mapzoom = 0;
  float jitter_time = 0.0f;
  u32 last_tick = 0;

  size_t flag_count = 0;
  GameFlag flags[256];

  // Current max green count is min((PrizeFactor * player_count) / 1000, 256)
  size_t green_count = 0;
  PrizeGreen greens[kMaxGreenCount];
  u32 green_ticks = 0;
  u32 last_green_tick = 0;
  u32 last_green_collision_tick = 0;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, int width, int height);

  bool Initialize(InputState& input);
  void Cleanup();

  bool Update(const InputState& input, float dt);
  void UpdateGreens(float dt);
  void SpawnDeathGreen(const Vector2f& position, Prize prize);

  void Render(float dt);

  void RenderGame(float dt);
  void RenderJoin(float dt);

  void RenderMenu();
  bool HandleMenuKey(int codepoint, int mods);

  void RecreateRadar();

  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagPosition(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnTurfFlagUpdate(u8* pkt, size_t size);
  void OnPlayerId(u8* pkt, size_t size);
};

}  // namespace null

#endif
