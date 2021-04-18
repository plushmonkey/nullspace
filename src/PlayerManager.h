#ifndef NULLSPACE_PLAYER_MANAGER_H_
#define NULLSPACE_PLAYER_MANAGER_H_

#include "Player.h"
#include "Types.h"

namespace null {

struct Connection;
struct PacketDispatcher;

struct PlayerManager {
  Connection& connection;
  size_t player_count = 0;
  Player players[1024];

  u16 player_id = 0;
  u32 last_position_tick = 0;

  PlayerManager(Connection& connection, PacketDispatcher& dispatcher);

  void Update(float dt);

  Player* GetSelf();
  Player* GetPlayerById(u16 id, size_t* index = nullptr);
  void SendPositionPacket();

  void OnPlayerIdChange(u8* pkt, size_t size);
  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerDeath(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);
  void OnLargePositionPacket(u8* pkt, size_t size);
  void OnSmallPositionPacket(u8* pkt, size_t size);

  void OnPositionPacket(Player& player, const Vector2f& position);
};

}  // namespace null

#endif
