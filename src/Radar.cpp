#include "Radar.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "Game.h"
#include "PlayerManager.h"
#include "Tick.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"
#include "render/TileRenderer.h"

namespace null {

constexpr float kRadarBorder = 6.0f;

void Radar::Update(Camera& ui_camera, short map_zoom, u16 team_freq, u16 spec_id) {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  if (map_zoom < 1) map_zoom = 1;

  s16 dim = ((((u16)ui_camera.surface_dim.x / 6) / 4) * 8) / 2;
  u32 full_dim = ((u32)ui_camera.surface_dim.x * 8) / map_zoom;

  // Use the same method as Continuum to generate visible texture
  u32 ivar8 = ((u32)ui_camera.surface_dim.x / 6) + ((u32)ui_camera.surface_dim.x >> 0x1F);
  s32 ivar5 = full_dim;
  u32 ivar6 = (u32)(self->position.y * 16) * ivar5;
  u32 ivar4 = ((ivar8 >> 2) - (ivar8 >> 0x1F)) * 8 * 4;

  ivar8 = (ivar4 + (ivar4 >> 0x1F & 7U)) >> 3;
  ivar4 = (u32)(self->position.x * 16) * ivar5;

  s32 texture_min_x = ((s32)(ivar4 + (ivar4 >> 0x1F & 0x3FFFU)) >> 0xE) - ivar8 / 2;
  s32 texture_min_y = ((s32)(ivar6 + (ivar6 >> 0x1F & 0x3FFFU)) >> 0xE) - ivar8 / 2;

  ivar5 = ivar5 - ivar8;

  if (texture_min_x < 0) {
    texture_min_x = 0;
  } else if (ivar5 < texture_min_x) {
    texture_min_x = ivar5;
  }

  if (texture_min_y < 0) {
    texture_min_y = 0;
  } else if (ivar5 < texture_min_y) {
    texture_min_y = ivar5;
  }

  s32 texture_max_x = texture_min_x + ivar8;
  s32 texture_max_y = texture_min_y + ivar8;

  ctx.radar_dim = Vector2f(dim, dim);
  ctx.radar_position = ui_camera.surface_dim - Vector2f(dim, dim) - Vector2f(kRadarBorder, kRadarBorder);
  ctx.min_uv = Vector2f(texture_min_x / (float)full_dim, texture_min_y / (float)full_dim);
  ctx.max_uv = Vector2f(texture_max_x / (float)full_dim, texture_max_y / (float)full_dim);

  ctx.world_min = Vector2f(ctx.min_uv.x * 1024.0f, ctx.min_uv.y * 1024.0f);
  ctx.world_max = Vector2f(ctx.max_uv.x * 1024.0f, ctx.max_uv.y * 1024.0f);
  ctx.world_dim = ctx.world_max - ctx.world_min;
  ctx.team_freq = team_freq;
  ctx.spec_id = spec_id;
}

void Radar::Render(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer, u16 map_zoom,
                   PrizeGreen* greens, size_t green_count) {
  if (tile_renderer.radar_texture == -1 || tile_renderer.full_radar_texture == -1) return;

  Player* self = player_manager.GetSelf();
  if (!self) return;

  SpriteRenderable visible;

  // Calculate uvs for displayable map
  visible.texture = tile_renderer.full_radar_renderable.texture;
  visible.dimensions = ctx.radar_dim;
  visible.uvs[0] = Vector2f(ctx.min_uv.x, ctx.min_uv.y);
  visible.uvs[1] = Vector2f(ctx.max_uv.x, ctx.min_uv.y);
  visible.uvs[2] = Vector2f(ctx.min_uv.x, ctx.max_uv.y);
  visible.uvs[3] = Vector2f(ctx.max_uv.x, ctx.max_uv.y);

  renderer.Draw(ui_camera, visible, ctx.radar_position, Layer::AfterChat);

  for (size_t i = 0; i < green_count; ++i) {
    PrizeGreen* green = greens + i;

    RenderIndicator(ui_camera, renderer, green->position, Vector2f(2, 2), 23);
  }

  RenderPlayers(ui_camera, renderer, *self);

  Graphics::DrawBorder(renderer, ui_camera, ctx.radar_position + ctx.radar_dim * 0.5f, ctx.radar_dim * 0.5f);

  RenderTime(ui_camera, renderer, ctx.radar_position, *self);
}

void Radar::RenderDecoy(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player,
                        const Vector2f& position) {
  IndicatorRenderable renderable = GetIndicator(self, player);

  if (ctx.team_freq == player.frequency) {
    renderable.sprite_index = 36;
  }

  RenderIndicator(ui_camera, renderer, position, renderable.dim, renderable.sprite_index);
}

void Radar::RenderPlayer(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player) {
  if (player.ship >= 8) return;

  bool visible =
      !(player.togglables & Status_Stealth) || self.togglables & Status_XRadar || player.frequency == ctx.team_freq;

  if (!visible) {
    return;
  }

  IndicatorRenderable renderable = GetIndicator(self, player);

  if (renderable.render) {
    RenderIndicator(ui_camera, renderer, player.position, renderable.dim, renderable.sprite_index);
  }
}

void Radar::RenderPlayers(Camera& ui_camera, SpriteRenderer& renderer, Player& self) {
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    RenderPlayer(ui_camera, renderer, self, *player);
  }
}

Radar::IndicatorRenderable Radar::GetIndicator(Player& self, Player& player) {
  IndicatorRenderable renderable;

  bool is_me = player.id == ctx.spec_id || (player.id == self.id && self.ship != 8);

  size_t sprite_index = 34;
  bool render = true;

  if (player.frequency == ctx.team_freq) {
    sprite_index = 29;
  } else {
    if (player.bounty > 100) {
      sprite_index = 33;
    }

    if (player.flags > 0) {
      sprite_index = 31;
    }
  }

  if (is_me) {
    sprite_index = 29;

    render = sin(GetCurrentTick() / 5) < 0;
  }

  renderable.sprite_index = sprite_index;
  renderable.dim = player.flags > 0 ? Vector2f(3, 3) : Vector2f(2, 2);
  renderable.render = render;

  return renderable;
}

void Radar::RenderIndicator(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& position, const Vector2f& dim,
                            size_t sprite_index) {
  Vector2f& min = ctx.world_min;
  Vector2f& max = ctx.world_max;
  Vector2f& world_dim = ctx.world_dim;

  if (position.x >= min.x && position.x < max.x && position.y >= min.y && position.y < max.y) {
    float u = (position.x - max.x) / world_dim.x + 1.0f;
    float v = (position.y - max.y) / world_dim.y + 1.0f;

    SpriteRenderable renderable = Graphics::color_sprites[sprite_index];
    renderable.dimensions = dim;

    Vector2f offset = Vector2f(ctx.radar_dim.x * u, ctx.radar_dim.y * v);
    renderer.Draw(ui_camera, renderable, ctx.radar_position + offset, Layer::TopMost);
  }
}

void Radar::RenderTime(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& radar_position, Player& self) {
  // Render text above radar
  time_t t;
  time(&t);

  tm* ti = localtime(&t);

  int hour = ti->tm_hour;
  bool pm = hour >= 12;

  if (hour > 12) {
    hour -= 12;
  }

  u32 map_coord_x = (u32)floor(self.position.x / (1024 / 20.0f));
  u32 map_coord_y = (u32)floor(self.position.y / (1024 / 20.0f)) + 1;

  char output[256];

  sprintf(output, "%d:%02d%s  %c%d", hour, ti->tm_min, pm ? "pm" : "am", map_coord_x + 'A', map_coord_y);

  renderer.DrawText(ui_camera, output, TextColor::White, Vector2f(ui_camera.surface_dim.x - 5, radar_position.y - 16),
                    Layer::TopMost, TextAlignment::Right);
}

void Radar::RenderFull(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer) {
  if (tile_renderer.radar_texture == -1) return;

  Player* self = player_manager.GetSelf();

  if (!self) return;

  SpriteRenderable& radar_renderable = tile_renderer.radar_renderable;
  Vector2f position = ui_camera.surface_dim - radar_renderable.dimensions - Vector2f(kRadarBorder, kRadarBorder);
  renderer.Draw(ui_camera, radar_renderable, position, Layer::TopMost);

  Vector2f half_extents = radar_renderable.dimensions * 0.5f;
  Graphics::DrawBorder(renderer, ui_camera, position + half_extents, half_extents);

  // TODO: Use the actual animation in the color file instead of manually blinking
  if (sin(GetCurrentTick() / 5) < 0) {
    Vector2f percent = self->position * (1.0f / 1024.0f);
    Vector2f start =
        position + Vector2f(percent.x * radar_renderable.dimensions.x, percent.y * radar_renderable.dimensions.y);

    SpriteRenderable self_renderable = Graphics::color_sprites[25];
    self_renderable.dimensions = Vector2f(2, 2);

    renderer.Draw(ui_camera, self_renderable, start, Layer::TopMost);
  }
}

}  // namespace null
