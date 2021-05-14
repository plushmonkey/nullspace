#ifndef NULLSPACE_RADAR_H_
#define NULLSPACE_RADAR_H_

#include "Math.h"
#include "Types.h"

namespace null {

struct Camera;
struct Player;
struct PlayerManager;
struct PrizeGreen;
struct SpriteRenderer;
struct TileRenderer;

struct Radar {
  PlayerManager& player_manager;

  Radar(PlayerManager& player_manager) : player_manager(player_manager) {}

  void Render(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer, u16 map_zoom, u16 team_freq,
              u16 spec_id, PrizeGreen* greens, size_t green_count);
  void RenderFull(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer);

 private:
  struct IndicatorContext {
    Vector2f radar_position;
    Vector2f radar_dim;
    Vector2f world_min;
    Vector2f world_max;
    Vector2f world_dim;
  };

  void RenderPlayers(Camera& ui_camera, SpriteRenderer& renderer, IndicatorContext& ctx, Player* self, u16 team_freq,
                     u16 spec_id);

  void RenderTime(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& radar_position, Player& self);

  void RenderIndicator(Camera& ui_camera, SpriteRenderer& renderer, IndicatorContext& ctx, const Vector2f& position,
                       const Vector2f& dim, size_t sprite_index);
};

}  // namespace null

#endif
