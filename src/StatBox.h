#ifndef NULLSPACE_STATBOX_H_
#define NULLSPACE_STATBOX_H_

#include "InputState.h"
#include "Math.h"
#include "PlayerManager.h"
#include "render/SpriteRenderer.h"

namespace null {

enum class StatViewType { Names, Points, PointSort, TeamSort, Full, Frequency, None };

struct Camera;
struct PacketDispatcher;
struct SpriteRenderer;

struct StatTextOutput {
  char text[32];
  Vector2f position;
  TextColor color;
  TextAlignment alignment;
};

struct StatRenderableOutput {
  SpriteRenderable* renderable;
  Vector2f position;
  Vector2f dimensions;
};

struct StatBox {
  PlayerManager& player_manager;
  StatViewType view_type = StatViewType::Names;

  size_t selected_index = -1;

  // PlayerId view into the player list
  u16 player_view[1024];

  Vector2f view_dimensions;

  size_t text_count = 0;
  StatTextOutput text_outputs[256];

  size_t renderable_count = 0;
  StatRenderableOutput renderable_outputs[256];

  StatBox(PlayerManager& player_manager, PacketDispatcher& dispatcher);

  void Render(Camera& camera, SpriteRenderer& renderer);

  void RecordNamesView(const Player& me);
  void RecordPointsView(const Player& me);
  void RecordTeamSortView(const Player& me);
  void RecordFullView(const Player& me);
  void RecordFrequencyView(const Player& me);

  void OnAction(InputAction action);
  void RecordView();
  void UpdateView();

  void SortView();
  void SortByName(const Player& self);
  void SortByPoints(const Player& self);
  void SortByFreq(const Player& self);

  Player* GetSelectedPlayer();
  float GetPointsWidth();
  float GetPointsSumWidth();

  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);

  StatTextOutput* AddTextOutput(const Vector2f& position, TextColor color, TextAlignment alignment);
  StatRenderableOutput* AddRenderableOutput(SpriteRenderable& renderable, const Vector2f& position,
                                            const Vector2f& dimensions);

 private:
  void RecordName(Player* player, float y, bool selected, bool same_freq);
};

}  // namespace null

#endif
