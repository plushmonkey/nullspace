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

  u32 tick = GetCurrentTick();

  for (size_t i = 0; i < temporary_indicator_count; ++i) {
    TemporaryRadarIndicator* temp_indicator = temporary_indicators + i;

    if (TICK_GT(tick, temp_indicator->end_tick)) {
      temporary_indicators[i--] = temporary_indicators[--temporary_indicator_count];
    }
  }
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

    IndicatorRenderable indicator;
    indicator.dim = Vector2f(2, 2);
    indicator.color = ColorType::RadarPrize;

    RenderIndicator(ui_camera, renderer, green->position, indicator);
  }

  RenderPlayers(ui_camera, renderer, *self);

  for (size_t i = 0; i < temporary_indicator_count; ++i) {
    TemporaryRadarIndicator* temp_indicator = temporary_indicators + i;

    RenderIndicator(ui_camera, renderer, temp_indicator->world_position, temp_indicator->indicator);
  }

  Graphics::DrawBorder(renderer, ui_camera, ctx.radar_position + ctx.radar_dim * 0.5f, ctx.radar_dim * 0.5f);

  RenderTime(ui_camera, renderer, ctx.radar_position, *self);
}

void Radar::RenderDecoy(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player,
                        const Vector2f& position) {
  IndicatorRenderable renderable = GetIndicator(self, player);

  if (ctx.team_freq == player.frequency) {
    renderable.color = ColorType::RadarTeamDecoy;
    renderable.dim = Vector2f(2, 2);
  }

  RenderIndicator(ui_camera, renderer, position, renderable);
}

void Radar::RenderPlayer(Camera& ui_camera, SpriteRenderer& renderer, Player& self, Player& player) {
  if (player.ship >= 8) return;
  if (player.attach_parent != kInvalidPlayerId) return;
  if (!player_manager.IsSynchronized(player)) return;

  bool visible =
      !(player.togglables & Status_Stealth) || self.togglables & Status_XRadar || player.frequency == ctx.team_freq;

  if (!visible && player.children != nullptr) {
    AttachInfo* info = player.children;

    // Loop through attached children and find any that don't have stealth
    while (info && !visible) {
      Player* child = player_manager.GetPlayerById(info->player_id);

      if (child) {
        visible = !(child->togglables & Status_Stealth);
      }

      info = info->next;
    }
  }

  if (!visible) return;

  IndicatorRenderable renderable = GetIndicator(self, player);

  RenderIndicator(ui_camera, renderer, player.position, renderable);
}

void Radar::RenderPlayers(Camera& ui_camera, SpriteRenderer& renderer, Player& self) {
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    RenderPlayer(ui_camera, renderer, self, *player);
  }
}

void Radar::AddTemporaryIndicator(const Vector2f& world_position, u32 end_tick, const Vector2f& dimensions,
                                  ColorType color) {
  if (temporary_indicator_count < NULLSPACE_ARRAY_SIZE(temporary_indicators)) {
    TemporaryRadarIndicator* temp_indicator = temporary_indicators + temporary_indicator_count++;
    temp_indicator->indicator.dim = dimensions;
    temp_indicator->indicator.color = color;
    temp_indicator->world_position = world_position;
    temp_indicator->end_tick = end_tick;
  }
}

IndicatorRenderable Radar::GetIndicator(Player& self, Player& player) {
  IndicatorRenderable renderable;

  bool is_me = player.id == ctx.spec_id || (player.id == self.id && self.ship != 8);

  ColorType color = ColorType::RadarEnemy;

  if (player.frequency == ctx.team_freq) {
    color = ColorType::RadarTeam;
  } else {
    if (player.bounty >= g_Settings.target_bounty) {
      color = ColorType::RadarEnemyTarget;
    }

    if (player.flags > 0 && player_manager.connection.settings.FlaggerOnRadar) {
      color = ColorType::RadarEnemyFlag;
    }
  }

  if (is_me) {
    color = ColorType::RadarSelf;
  }

  renderable.color = color;
  renderable.dim = player.flags > 0 ? Vector2f(3, 3) : Vector2f(2, 2);

  return renderable;
}

void Radar::RenderIndicator(Camera& ui_camera, SpriteRenderer& renderer, const Vector2f& position,
                            const IndicatorRenderable& indicator) {
  Vector2f& min = ctx.world_min;
  Vector2f& max = ctx.world_max;
  Vector2f& world_dim = ctx.world_dim;

  if (position.x >= min.x && position.x < max.x && position.y >= min.y && position.y < max.y) {
    float u = (position.x - max.x) / world_dim.x + 1.0f;
    float v = (position.y - max.y) / world_dim.y + 1.0f;

    SpriteRenderable renderable = Graphics::GetColor(indicator.color, indicator.dim);

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

  Vector2f percent = self->position * (1.0f / 1024.0f);
  Vector2f start =
      position + Vector2f(percent.x * radar_renderable.dimensions.x, percent.y * radar_renderable.dimensions.y);

  SpriteRenderable self_renderable = Graphics::GetColor(ColorType::RadarSelf, Vector2f(2, 2));

  renderer.Draw(ui_camera, self_renderable, start, Layer::TopMost);
}

bool Radar::InRadarView(const Vector2f& position) {
  return BoxContainsPoint(ctx.world_min, ctx.world_max, position);
}

}  // namespace null
