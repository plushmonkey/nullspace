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

  void Update(Camera& ui_camera, short map_zoom, u16 team_freq, u16 spec_id);

  void Render(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer, u16 map_zoom,
              PrizeGreen* greens, size_t green_count);
  void RenderFull(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer);

  void RenderDecoy(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player, const Vector2f& position);

 private:
  struct Context {
    Vector2f radar_position;
    Vector2f radar_dim;
    Vector2f world_min;
    Vector2f world_max;
    Vector2f world_dim;

    Vector2f min_uv;
    Vector2f max_uv;

    float team_freq;
    u16 spec_id;
  };

  struct IndicatorRenderable {
    bool render;
    Vector2f dim;
    size_t sprite_index;
  };

  Context ctx;

  void RenderPlayer(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player);
  void RenderPlayers(Camera& ui_camera, SpriteRenderer& renderer, Player& self);

  void RenderIndicator(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& position, const Vector2f& dim,
                       size_t sprite_index);

  void RenderTime(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& radar_position, Player& self);

  IndicatorRenderable GetIndicator(Player& self, Player& player);
};

}  // namespace null

#endif
