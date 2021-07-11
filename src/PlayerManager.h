#ifndef NULLSPACE_PLAYER_MANAGER_H_
#define NULLSPACE_PLAYER_MANAGER_H_

#include "BannerPool.h"
#include "Notification.h"
#include "Player.h"
#include "Types.h"
#include "render/Animation.h"
#include "render/Graphics.h"

namespace null {

struct Camera;
struct ChatController;
struct Connection;
struct InputState;
struct PacketDispatcher;
struct ShipController;
struct SpectateView;
struct SoundSystem;
struct SpriteRenderer;
struct WeaponManager;

struct PlayerManager {
  MemoryArena& perm_arena;
  Connection& connection;
  SoundSystem& sound_system;
  WeaponManager* weapon_manager = nullptr;
  ShipController* ship_controller = nullptr;
  ChatController* chat_controller = nullptr;
  NotificationSystem* notifications = nullptr;
  SpectateView* specview = nullptr;
  BannerPool* banners = nullptr;

  u16 player_id = 0;
  u32 last_position_tick = 0;
  bool received_initial_list = false;

  AttachInfo* attach_free = nullptr;

  Animation explode_animation;
  Animation warp_animation;
  Animation bombflash_animation;

  size_t player_count = 0;
  Player players[1024];

  // Indirection table to look up player by id quickly
  u16 player_lookup[65536];

  PlayerManager(MemoryArena& perm_arena, Connection& connection, PacketDispatcher& dispatcher,
                SoundSystem& sound_system);

  inline void Initialize(WeaponManager* weapon_manager, ShipController* ship_controller,
                         ChatController* chat_controller, NotificationSystem* notifications, SpectateView* specview,
                         BannerPool* banners) {
    this->weapon_manager = weapon_manager;
    this->ship_controller = ship_controller;
    this->chat_controller = chat_controller;
    this->notifications = notifications;
    this->specview = specview;
    this->banners = banners;

    warp_animation.sprite = &Graphics::anim_ship_warp;
    explode_animation.sprite = &Graphics::anim_ship_explode;
    bombflash_animation.sprite = &Graphics::anim_bombflash;
  }

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);

  void RenderPlayerName(Camera& camera, SpriteRenderer& renderer, Player& self, Player& player,
                        const Vector2f& position, bool is_decoy);

  void Spawn(bool reset = true);

  Player* GetSelf();
  Player* GetPlayerById(u16 id, size_t* index = nullptr);
  inline u16 GetPlayerIndex(u16 id) { return player_lookup[id]; }

  void SendPositionPacket();
  void SimulatePlayer(Player& player, float dt);
  bool SimulateAxis(Player& player, float dt, int axis);

  void OnPlayerIdChange(u8* pkt, size_t size);
  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerDeath(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);
  void OnLargePositionPacket(u8* pkt, size_t size);
  void OnSmallPositionPacket(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnCreateTurretLink(u8* pkt, size_t size);
  void OnDestroyTurretLink(u8* pkt, size_t size);

  void OnPositionPacket(Player& player, const Vector2f& position);

  void AttachSelf(Player* destination);
  void AttachPlayer(Player& requester, Player& destination);
  void DetachPlayer(Player& player);
  void DetachAllChildren(Player& player);
  size_t GetTurretCount(Player& player);

  inline bool IsSynchronized(Player& player) {
    return player.id == player_id || TICK_DIFF(GetCurrentTick(), player.GetTimestamp()) < kPlayerTimeout;
  }
};

}  // namespace null

#endif
