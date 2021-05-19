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
struct SpriteRenderer;

struct PlayerManager {
  Connection& connection;
  ShipController* ship_controller = nullptr;
  ChatController* chat_controller = nullptr;
  NotificationSystem* notifications = nullptr;
  size_t player_count = 0;
  Player players[1024];

  u16 player_id = 0;
  u32 last_position_tick = 0;
  bool received_initial_list = false;

  PlayerManager(Connection& connection, PacketDispatcher& dispatcher);

  inline void Initialize(ShipController* ship_controller, ChatController* chat_controller,
                         NotificationSystem* notifications) {
    this->ship_controller = ship_controller;
    this->chat_controller = chat_controller;
    this->notifications = notifications;
  }

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer, u32 self_freq);

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

  void OnPositionPacket(Player& player, const Vector2f& position);
};

}  // namespace null

#endif
