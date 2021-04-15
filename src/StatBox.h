#ifndef NULLSPACE_STATBOX_H_
#define NULLSPACE_STATBOX_H_

#include "PlayerManager.h"

namespace null {

enum class StatViewType { Names, Points, PointSort, TeamSort, Full, Frequency, None };

struct Camera;
struct PacketDispatcher;
struct SpriteRenderer;

struct StatBox {
  PlayerManager& player_manager;
  StatViewType view_type = StatViewType::Names;

  size_t selected_index = 0;
  Player* selected_player = nullptr;

  // PlayerId view into the player list
  u16 player_view[1024];

  StatBox(PlayerManager& player_manager, PacketDispatcher& dispatcher);

  void Render(Camera& camera, SpriteRenderer& renderer);

  void OnCharacterPress(int codepoint, bool control);
  void SortView();
  void UpdateView();

  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);

 private:
  void RenderName(Camera& camera, SpriteRenderer& renderer, Player* player, float y, bool selected, bool same_freq);
};

}  // namespace null

#endif
