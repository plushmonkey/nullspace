#include "Game.h"

#include <cstdio>

#include "Memory.h"
#include "Tick.h"
#include "render/Animation.h"
#include "render/Graphics.h"

namespace null {

#define SIM_TEST 0

void Simulate(Connection& connection, PlayerManager& player_manager, float dt);

void OnCharacterPress(void* user, int codepoint, int mods) {
  Game* game = (Game*)user;

  game->chat.OnCharacterPress(codepoint, mods);
  game->statbox.OnCharacterPress(codepoint, mods);
  game->specview.OnCharacterPress(codepoint, mods);
}

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      animation(),
      dispatcher(),
      connection(perm_arena, temp_arena, dispatcher),
      player_manager(connection, dispatcher),
      weapon_manager(connection, player_manager, dispatcher, animation),
      camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(60.0f),
      statbox(player_manager, dispatcher),
      chat(dispatcher, connection, player_manager, statbox),
      specview(connection, statbox) {
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -1.0f, 1.0f);
}

bool Game::Initialize(InputState& input) {
  if (!tile_renderer.Initialize()) {
    return false;
  }

  if (!sprite_renderer.Initialize(perm_arena)) {
    return false;
  }

  if (!Graphics::Initialize(sprite_renderer)) {
    fprintf(stderr, "Failed to initialize graphics.\n");
    return false;
  }

  input.SetCallback(OnCharacterPress, this);

  return true;
}

void Game::Update(const InputState& input, float dt) {
  player_manager.Update(dt);
  weapon_manager.Update(dt);

  Simulate(connection, player_manager, dt);
  specview.Update(input, dt);

  Player* me = player_manager.GetSelf();
  if (me) {
    // camera.position = me->position;
  }

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    if (!tile_renderer.CreateMapBuffer(temp_arena, connection.map_handler.filename, ui_camera.surface_dim)) {
      fprintf(stderr, "Failed to create map/radar.\n");
    }

    if (me) {
      me->position = Vector2i(512 * 16 * 1000, 512 * 16 * 1000);
    }
  }

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;

    player->position.x += player->velocity.x;
    player->position.y += player->velocity.y;

    player->explode_animation.t += dt;
    player->warp_animation.t += dt;

    if (player->enter_delay > 0.0f) {
      player->enter_delay -= dt;

      if (!player->explode_animation.IsAnimating()) {
        player->position = Vector2i(0, 0);
        player->velocity = Vector2s(0, 0);
        player->lerp_time = 0.0f;
      }
    }

    if (player->lerp_time > 0.0f) {
      player->position.x += player->lerp_velocity.x;
      player->position.y += player->lerp_velocity.y;
      player->lerp_time -= dt;
    }
  }

  // Cap player and spectator camera to playable area
  if (me) {
    if (me->position.x < 0) me->position.x = 0;
    if (me->position.y < 0) me->position.y = 0;
    if (me->position.x > 1023 * 16 * 1000) me->position.x = 1023 * 16 * 1000;
    if (me->position.y > 1023 * 16 * 1000) me->position.y = 1023 * 16 * 1000;
    camera.position.x = me->position.x / 1000.0f;
    camera.position.y = me->position.y / 1000.0f;
  }

  render_radar = input.IsDown(InputAction::DisplayMap);
}

void Game::Render(float dt) {
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

  animation.Update(dt);

  tile_renderer.Render(camera);

  Player* me = player_manager.GetSelf();

  // TODO: Formalize layers
  // Draw animations and weapons before ships and names so they are below
  animation.Render(camera, sprite_renderer);
  weapon_manager.Render(camera, sprite_renderer);

  // Draw player ships
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;
    if (player->position.x == 0 && player->position.y == 0) continue;

    if (player->explode_animation.IsAnimating()) {
      SpriteRenderable& renderable = player->explode_animation.GetFrame();
      Vector2f position =
          Vector2f(player->position.x / 1000.0f, player->position.y / 1000.0f) - renderable.dimensions * 0.5f;

      sprite_renderer.Draw(camera, renderable, position);
    } else if (player->enter_delay <= 0.0f) {
      size_t index = player->ship * 40 + player->direction;

      Vector2f position = Vector2f(player->position.x / 1000.0f, player->position.y / 1000.0f) -
                          Graphics::ship_sprites[index].dimensions * 0.5f;

      sprite_renderer.Draw(camera, Graphics::ship_sprites[index], position);

      if (player->warp_animation.IsAnimating()) {
        SpriteRenderable& renderable = player->warp_animation.GetFrame();
        Vector2f position =
            Vector2f(player->position.x / 1000.0f, player->position.y / 1000.0f) - renderable.dimensions * 0.5f;

        sprite_renderer.Draw(camera, renderable, position);
      }
    }
  }

  // Draw player names - This is done in separate loop to batch sprite sheet renderables
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;
    if (player->position.x == 0 && player->position.y == 0) continue;

    if (player->enter_delay <= 0.0f) {
      float radius = connection.settings.ShipSettings[player->ship].GetRadius();

      char display[32];
      sprintf(display, "%s(%d)[%d]", player->name, player->bounty, player->ping * 10);

      u32 team_freq = me->frequency;

      if (me->ship == 8) {
        team_freq = specview.spectate_frequency;
      }

      if (me) {
        TextColor color = team_freq == player->frequency ? TextColor::Yellow : TextColor::Blue;
        Vector2f position(player->position.x / 1000.0f + radius, player->position.y / 1000.0f + radius);

        sprite_renderer.DrawText(camera, display, color, position);
      }
    }
  }

  sprite_renderer.Render(camera);

  RenderRadar(me);

  chat.Render(ui_camera, sprite_renderer);

  // TODO: Move all of this out

  if (connection.login_state == Connection::LoginState::MapDownload) {
    char downloading[64];

    sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                         ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f);

    int percent =
        (int)(connection.packet_sequencer.huge_chunks.size * 100 / (float)connection.map_handler.compressed_size);

    sprintf(downloading, "Downloading level: %d%%", percent);
    Vector2f download_pos(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);
    sprite_renderer.DrawText(ui_camera, downloading, TextColor::Blue, download_pos, TextAlignment::Center);
  } else if (connection.login_state < Connection::LoginState::MapDownload) {
    sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                         ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f);

    Vector2f position(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);

    sprite_renderer.DrawText(ui_camera, "Entering arena", TextColor::Blue, position, TextAlignment::Center);
  }

  if (me && connection.login_state == Connection::LoginState::Complete) {
    statbox.Render(ui_camera, sprite_renderer);
  }

  char fps_text[32];
  sprintf(fps_text, "FPS: %d", (int)(fps + 0.5f));
  sprite_renderer.DrawText(ui_camera, fps_text, TextColor::Pink, Vector2f(ui_camera.surface_dim.x, 0),
                           TextAlignment::Right);

  sprite_renderer.Render(ui_camera);
}

void Game::RenderRadar(Player* me) {
  if (tile_renderer.radar_renderable.texture != -1 && me) {
    SpriteRenderable& radar_renderable = tile_renderer.radar_renderable;
    float border = 6.0f;

    if (render_radar) {
      Vector2f position = ui_camera.surface_dim - radar_renderable.dimensions - Vector2f(border, border);
      sprite_renderer.Draw(ui_camera, radar_renderable, position);

      Vector2f half_extents = radar_renderable.dimensions * 0.5f;
      Graphics::DrawBorder(sprite_renderer, ui_camera, position + half_extents, half_extents);

      if (sin(GetCurrentTick() / 5) < 0) {
        Vector2f percent(me->position.x / 1000.0f * (1.0f / (1024.0f * 16.0f)),
                         me->position.y / 1000.0f * (1.0f / (1024.0f * 16.0f)));
        Vector2f start =
            position + Vector2f(percent.x * radar_renderable.dimensions.x, percent.y * radar_renderable.dimensions.y);

        SpriteRenderable self_renderable = Graphics::color_sprites[25];
        self_renderable.dimensions = Vector2f(2, 2);

        sprite_renderer.Draw(ui_camera, self_renderable, start);
      }
    } else {
      // calculate uvs for displayable map
      SpriteRenderable visible;
      visible.texture = radar_renderable.texture;
      // TODO: find real width
      s16 dim = (s16)(ui_camera.surface_dim.x * 0.165f);
      u16 map_zoom = connection.settings.MapZoomFactor;
      s16 range = (s16)((map_zoom / 48.0f) * 512.0f * 16.0f);

      Vector2s center(me->position.x / 1000, me->position.y / 1000);

      // Cap the radar to map range
      if (center.x - range < 0) center.x = range;
      if (center.y - range < 0) center.y = range;
      if (center.x + range > 1024 * 16) center.x = 16 * 1024 - range;
      if (center.y + range > 1024 * 16) center.y = 16 * 1024 - range;

      Vector2s min(center.x - range, center.y - range);
      Vector2s max(center.x + range, center.y + range);

      float uv_multiplier = (1.0f / (1024.0f * 16.0f));
      Vector2f min_uv(min.x * uv_multiplier, min.y * uv_multiplier);
      Vector2f max_uv(max.x * uv_multiplier, max.y * uv_multiplier);

      visible.dimensions = Vector2f(dim, dim);
      visible.uvs[0] = Vector2f(min_uv.x, min_uv.y);
      visible.uvs[1] = Vector2f(max_uv.x, min_uv.y);
      visible.uvs[2] = Vector2f(min_uv.x, max_uv.y);
      visible.uvs[3] = Vector2f(max_uv.x, max_uv.y);

      Vector2f position = ui_camera.surface_dim - Vector2f(dim, dim) - Vector2f(border, border);

      sprite_renderer.Draw(ui_camera, visible, position);

      Vector2f half_extents(dim * 0.5f, dim * 0.5f);

      u32 team_freq = me->frequency;

      if (me->ship >= 8) {
        team_freq = specview.spectate_frequency;
      }

      for (size_t i = 0; i < player_manager.player_count; ++i) {
        Player* player = player_manager.players + i;

        if (player->ship >= 8) continue;

        Vector2f p((float)(player->position.x / 1000), (float)(player->position.y / 1000));
        Vector2f percent((p.x - center.x) * (1.0f / range), (p.y - center.y) * (1.0f / range));

        if (p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y) {
          size_t sprite_index = 34;
          bool render = true;

          if (player->bounty > 100) {
            sprite_index = 33;
          }

          if (player->frequency == team_freq) {
            sprite_index = 29;
          }

          bool is_me = player->id == specview.spectate_id || (player == me && me->ship != 8);

          if (is_me) {
            sprite_index = 29;

            render = sin(GetCurrentTick() / 5) < 0;
          }

          if (render) {
            SpriteRenderable renderable = Graphics::color_sprites[sprite_index];
            Vector2f center_radar = position + half_extents;
            Vector2f start = center_radar + Vector2f(percent.x * half_extents.x, percent.y * half_extents.y);

            renderable.dimensions = Vector2f(2, 2);

            sprite_renderer.Draw(ui_camera, renderable, start);
          }
        }
      }

      Graphics::DrawBorder(sprite_renderer, ui_camera, position + half_extents, half_extents);
    }
  }
}

// Test fun code
void Simulate(Connection& connection, PlayerManager& player_manager, float dt) {
  static int last_request = 0;

  if (connection.login_state != Connection::LoginState::Complete) return;

  Player* player = player_manager.GetSelf();
  if (!player) return;

#if SIM_TEST
  if (player->ship == 8 && TICK_DIFF(GetCurrentTick(), last_request) > 300) {
    player->ship = 1;
    player->position = Vector2f(512, 512);

#pragma pack(push, 1)
    struct {
      u8 type;
      u8 ship;
    } request = {0x18, player->ship};
#pragma pack(pop)

    printf("Sending ship request packet\n");

    connection.packet_sequencer.SendReliableMessage(connection, (u8*)&request, sizeof(request));
    last_request = GetCurrentTick();
    return;
  }

  // static Vector2f waypoints[] = {Vector2f(570, 465), Vector2f(420, 450), Vector2f(480, 585), Vector2f(585, 545)};
  static Vector2f waypoints[] = {Vector2f(570, 455), Vector2f(455, 455), Vector2f(455, 570), Vector2f(570, 570)};
  static size_t waypoint_index = 0;

  Vector2f target = waypoints[waypoint_index];

  player->velocity = Normalize(target - player->position) * 12.0f;
  player->weapon.level = 1;
  player->weapon.type = 2;

  float rads = std::atan2(target.y - player->position.y, target.x - player->position.x);
  float angle = rads * (180.0f / 3.14159f);
  int rot = (int)std::round(angle / 9.0f) + 10;

  if (rot < 0) {
    rot += 40;
  }

  player->direction = rot;

  if (target.DistanceSq(player->position) <= 2.0f * 2.0f) {
    waypoint_index = (waypoint_index + 1) % NULLSPACE_ARRAY_SIZE(waypoints);
  }
#else
    // player->position = Vector2f(512, 512);
#endif
}

}  // namespace null
