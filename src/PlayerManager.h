#ifndef NULLSPACE_PLAYER_MANAGER_H_
#define NULLSPACE_PLAYER_MANAGER_H_

#include "Notification.h"
#include "Player.h"
#include "Types.h"

namespace null {

struct Camera;
struct ChatController;
struct Connection;
struct InputState;
struct PacketDispatcher;
struct ShipController;
struct SpectateView;
struct SpriteRenderer;
struct WeaponManager;

struct PlayerManager {
  MemoryArena& perm_arena;
  Connection& connection;
  WeaponManager* weapon_manager = nullptr;
  ShipController* ship_controller = nullptr;
  ChatController* chat_controller = nullptr;
  NotificationSystem* notifications = nullptr;
  SpectateView* specview = nullptr;

  u16 player_id = 0;
  u32 last_position_tick = 0;
  bool received_initial_list = false;

  AttachInfo* attach_free = nullptr;

  size_t player_count = 0;
  Player players[1024];

  // Indirection table to look up player by id quickly
  u16 player_lookup[65536];

  PlayerManager(MemoryArena& perm_arena, Connection& connection, PacketDispatcher& dispatcher);

  inline void Initialize(WeaponManager* weapon_manager, ShipController* ship_controller,
                         ChatController* chat_controller, NotificationSystem* notifications, SpectateView* specview) {
    this->weapon_manager = weapon_manager;
    this->ship_controller = ship_controller;
    this->chat_controller = chat_controller;
    this->notifications = notifications;
    this->specview = specview;
  }

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);

  void RenderPlayerName(Camera& camera, SpriteRenderer& renderer, Player& self, Player& player,
                        const Vector2f& position, bool is_decoy);

  void Spawn();

  Player* GetSelf();
  Player* GetPlayerById(u16 id, size_t* index = nullptr);
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
  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnCreateTurretLink(u8* pkt, size_t size);
  void OnDestroyTurretLink(u8* pkt, size_t size);

  void OnPositionPacket(Player& player, const Vector2f& position);

  void AttachPlayer(Player& requester, Player& destination);
  void DetachPlayer(Player& player);
  void DetachAllChildren(Player& player);
};

}  // namespace null

#endif
