#ifndef NULLSPACE_PLAYER_MANAGER_H_
#define NULLSPACE_PLAYER_MANAGER_H_

#include <null/BannerPool.h>
#include <null/Notification.h>
#include <null/Player.h>
#include <null/Types.h>
#include <null/net/Connection.h>
#include <null/render/Animation.h>
#include <null/render/Graphics.h>

namespace null {

struct Camera;
struct ChatController;
struct InputState;
struct PacketDispatcher;
struct Radar;
struct ShipController;
struct SpectateView;
struct Soccer;
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
  Soccer* soccer = nullptr;
  SpectateView* specview = nullptr;
  BannerPool* banners = nullptr;
  Radar* radar = nullptr;

  u16 player_id = 0;
  // This is in server time
  s32 last_position_tick = 0;
  bool received_initial_list = false;
  bool requesting_attach = false;

  AttachInfo* attach_free = nullptr;

  Animation explode_animation;
  Animation warp_animation;
  Animation bombflash_animation;

  u32 last_send_damage_tick = 0;
  size_t damage_count = 0;
  Damage damages[10];

  size_t player_count = 0;
  Player players[1024];

  // Indirection table to look up player by id quickly
  u16 player_lookup[65536];

  PlayerManager(MemoryArena& perm_arena, Connection& connection, PacketDispatcher& dispatcher,
                SoundSystem& sound_system);

  inline void Initialize(WeaponManager* weapon_manager, ShipController* ship_controller,
                         ChatController* chat_controller, NotificationSystem* notifications, SpectateView* specview,
                         BannerPool* banners, Radar* radar, Soccer* soccer) {
    this->weapon_manager = weapon_manager;
    this->ship_controller = ship_controller;
    this->chat_controller = chat_controller;
    this->notifications = notifications;
    this->specview = specview;
    this->banners = banners;
    this->radar = radar;
    this->soccer = soccer;

    warp_animation.sprite = &Graphics::anim_ship_warp;
    explode_animation.sprite = &Graphics::anim_ship_explode;
    bombflash_animation.sprite = &Graphics::anim_bombflash;
  }

  void Update(float dt);
  void SynchronizePosition();

  void Render(Camera& camera, SpriteRenderer& renderer);

  void RenderPlayerName(Camera& camera, SpriteRenderer& renderer, Player& self, Player& player,
                        const Vector2f& position, bool is_decoy);

  void Spawn(bool reset = true);

  Player* GetSelf();
  Player* GetPlayerById(u16 id, size_t* index = nullptr);
  Player* GetPlayerByName(const char* name, size_t* index = nullptr);
  inline u16 GetPlayerIndex(u16 id) { return player_lookup[id]; }

  void RemovePlayer(Player* player);

  void PushDamage(PlayerId shooter_id, WeaponData weapon_data, int energy, int damage);

  void SendPositionPacket();
  void SimulatePlayer(Player& player, float dt, bool extrapolating);
  bool SimulateAxis(Player& player, float dt, int axis, bool extrapolating);

  void OnPlayerIdChange(u8* pkt, size_t size);
  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerDeath(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);
  void OnPlayerFrequencyChange(u8* pkt, size_t size);
  void OnLargePositionPacket(u8* pkt, size_t size);
  void OnBatchedLargePositionPacket(u8* pkt, size_t size);
  void OnSmallPositionPacket(u8* pkt, size_t size);
  void OnBatchedSmallPositionPacket(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnCreateTurretLink(u8* pkt, size_t size);
  void OnDestroyTurretLink(u8* pkt, size_t size);

  void OnPositionPacket(Player& player, const Vector2f& position, const Vector2f& velocity, s32 sim_ticks);

  void AttachSelf(Player* destination);
  void AttachPlayer(Player& requester, Player& destination);
  void DetachPlayer(Player& player);
  void DetachAllChildren(Player& player);
  size_t GetTurretCount(Player& player);

  bool IsAntiwarped(Player& self, bool notify);

  inline bool IsSynchronized(Player& player) {
    u16 tick = (GetCurrentTick() + connection.time_diff) & 0x7FFF;
    return player.id == player_id || SMALL_TICK_DIFF(tick, player.timestamp) < kPlayerTimeout;
  }
};

}  // namespace null

#endif
