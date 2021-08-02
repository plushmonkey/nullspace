#ifndef NULLSPACE_STATBOX_H_
#define NULLSPACE_STATBOX_H_

#include "InputState.h"
#include "Math.h"
#include "PlayerManager.h"
#include "render/SpriteRenderer.h"

namespace null {

enum class StatViewType { Names, Points, PointSort, TeamSort, Full, Frequency, None };

struct BannerPool;
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

struct SlidingView {
  size_t top;
  size_t size;
  size_t max_size;

  inline size_t begin() { return top; }
  inline size_t end() { return top + size; }
  inline size_t count() { return end() - begin(); }

  inline void Increment() {
    if (++size > max_size) {
      size = max_size;
    }
  }

  inline void Decrement() {
    if (size > 1) {
      --size;
    }
  }
};

struct StatBox {
  PlayerManager& player_manager;
  BannerPool& banners;

  StatViewType view_type = StatViewType::Names;
  bool rebuild = true;

  size_t selected_index = -1;

  SlidingView sliding_view;

  SpriteRenderable separator_renderable;

  // PlayerId view into the player list
  u16 player_view[1024];

  Vector2f view_dimensions;

  size_t text_count = 0;
  StatTextOutput text_outputs[256];

  size_t renderable_count = 0;
  StatRenderableOutput renderable_outputs[256];

  StatBox(PlayerManager& player_manager, BannerPool& banners, PacketDispatcher& dispatcher);

  void Render(Camera& camera, SpriteRenderer& renderer);

  void RecordNamesView(const Player& me);
  void RecordPointsView(const Player& me);
  void RecordTeamSortView(const Player& me);
  void RecordFullView(const Player& me);
  void RecordFrequencyView(const Player& me);

  void OnAction(InputAction action, bool menu);
  void RecordView();
  void UpdateView();
  void TriggerRebuild();

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
  void OnPlayerFrequencyChange(u8* pkt, size_t size);

  StatTextOutput* AddTextOutput(const Vector2f& position, TextColor color, TextAlignment alignment);
  StatRenderableOutput* AddRenderableOutput(SpriteRenderable* renderable, const Vector2f& position,
                                            const Vector2f& dimensions);

 private:
  void RecordName(Player* player, float y, bool selected, bool same_freq);
  SpriteRenderable* GetSeparatorRenderable();

  void UpdateSlidingView();
};

}  // namespace null

#endif
