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

void Radar::Render(Camera& ui_camera, SpriteRenderer& renderer, TileRenderer& tile_renderer, u16 map_zoom,
                   u16 team_freq, u16 spec_id, PrizeGreen* greens, size_t green_count) {
  if (tile_renderer.radar_texture == -1 || tile_renderer.full_radar_texture == -1) return;

  Player* self = player_manager.GetSelf();

  if (!self) return;

  float border = 6.0f;

  if (map_zoom < 1) {
    map_zoom = 1;
  }

  // calculate uvs for displayable map
  SpriteRenderable visible;

  visible.texture = tile_renderer.full_radar_renderable.texture;

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

  float uv_x_1 = texture_min_x / (float)full_dim;
  float uv_y_1 = texture_min_y / (float)full_dim;

  float uv_x_2 = texture_max_x / (float)full_dim;
  float uv_y_2 = texture_max_y / (float)full_dim;

  Vector2f min_uv(uv_x_1, uv_y_1);
  Vector2f max_uv(uv_x_2, uv_y_2);

  visible.dimensions = Vector2f(dim, dim);
  visible.uvs[0] = Vector2f(min_uv.x, min_uv.y);
  visible.uvs[1] = Vector2f(max_uv.x, min_uv.y);
  visible.uvs[2] = Vector2f(min_uv.x, max_uv.y);
  visible.uvs[3] = Vector2f(max_uv.x, max_uv.y);

  Vector2f position = ui_camera.surface_dim - Vector2f(dim, dim) - Vector2f(border, border);

  renderer.Draw(ui_camera, visible, position, Layer::TopMost);

  IndicatorContext ctx;

  ctx.radar_dim = Vector2f(dim, dim);
  ctx.radar_position = position;

  ctx.world_min = Vector2f(uv_x_1 * 1024.0f, uv_y_1 * 1024.0f);
  ctx.world_max = Vector2f(uv_x_2 * 1024.0f, uv_y_2 * 1024.0f);
  ctx.world_dim = ctx.world_max - ctx.world_min;

  for (size_t i = 0; i < green_count; ++i) {
    PrizeGreen* green = greens + i;

    RenderIndicator(ui_camera, renderer, ctx, green->position, Vector2f(2, 2), 23);
  }

  RenderPlayers(ui_camera, renderer, ctx, self, team_freq, spec_id);

  Graphics::DrawBorder(renderer, ui_camera, position + ctx.radar_dim * 0.5f, ctx.radar_dim * 0.5f);

  RenderTime(ui_camera, renderer, position, *self);
}

void Radar::RenderPlayers(Camera& ui_camera, SpriteRenderer& renderer, IndicatorContext& ctx, Player* self,
                          u16 team_freq, u16 spec_id) {
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship >= 8) continue;

    if ((player->togglables & Status_Stealth) && !(self->togglables & Status_XRadar) &&
        player->frequency != team_freq) {
      continue;
    }

    bool is_me = player->id == spec_id || (player == self && self->ship != 8);

    size_t sprite_index = 34;
    bool render = true;

    if (player->frequency == team_freq) {
      sprite_index = 29;
    } else {
      if (player->bounty > 100) {
        sprite_index = 33;
      }

      if (player->flags > 0) {
        sprite_index = 31;
      }
    }

    if (is_me) {
      sprite_index = 29;

      render = sin(GetCurrentTick() / 5) < 0;
    }

    if (render) {
      Vector2f dim = player->flags > 0 ? Vector2f(3, 3) : Vector2f(2, 2);
      RenderIndicator(ui_camera, renderer, ctx, player->position, dim, sprite_index);
    }
  }
}

void Radar::RenderIndicator(Camera& ui_camera, SpriteRenderer& renderer, IndicatorContext& ctx,
                            const Vector2f& position, const Vector2f& dim, size_t sprite_index) {
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

  float border = 6.0f;

  SpriteRenderable& radar_renderable = tile_renderer.radar_renderable;
  Vector2f position = ui_camera.surface_dim - radar_renderable.dimensions - Vector2f(border, border);
  renderer.Draw(ui_camera, radar_renderable, position, Layer::TopMost);

  Vector2f half_extents = radar_renderable.dimensions * 0.5f;
  Graphics::DrawBorder(renderer, ui_camera, position + half_extents, half_extents);

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
