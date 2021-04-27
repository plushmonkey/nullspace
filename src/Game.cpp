#include "Game.h"

#include <cassert>
#include <cstdio>

#include "Memory.h"
#include "Tick.h"
#include "render/Animation.h"
#include "render/Graphics.h"

namespace null {

static void OnCharacterPress(void* user, int codepoint, int mods) {
  Game* game = (Game*)user;

  game->chat.OnCharacterPress(codepoint, mods);
  game->statbox.OnCharacterPress(codepoint, mods);
  game->specview.OnCharacterPress(codepoint, mods);

  if (codepoint == NULLSPACE_KEY_ESCAPE) {
    game->menu_open = !game->menu_open;
    game->chat.display_full = game->menu_open;
  } else if (game->menu_open) {
    game->HandleMenuKey(codepoint, mods);
  }
}

static void OnFlagClaimPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagClaim(pkt, size);
}

static void OnFlagPositionPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagPosition(pkt, size);
}

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      animation(),
      dispatcher(),
      connection(perm_arena, temp_arena, dispatcher),
      player_manager(connection, dispatcher),
      weapon_manager(connection, player_manager, dispatcher, animation),
      camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f / 16.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(60.0f),
      statbox(player_manager, dispatcher),
      chat(dispatcher, connection, player_manager, statbox),
      specview(connection, statbox),
      lvz(perm_arena, temp_arena, connection.requester, sprite_renderer, dispatcher) {
  float zmax = (float)Layer::Count;
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -zmax, zmax);
  dispatcher.Register(ProtocolS2C::FlagPosition, OnFlagPositionPkt, this);
  dispatcher.Register(ProtocolS2C::FlagClaim, OnFlagClaimPkt, this);
}

bool Game::Initialize(InputState& input) {
  if (!tile_renderer.Initialize()) {
    fprintf(stderr, "Failed to initialize tile renderer.\n");
    return false;
  }

  if (!sprite_renderer.Initialize(perm_arena)) {
    fprintf(stderr, "Failed to initialize sprite renderer.\n");
    return false;
  }

  if (!Graphics::Initialize(sprite_renderer)) {
    fprintf(stderr, "Failed to initialize graphics.\n");
    return false;
  }

  if (!animated_tile_renderer.Initialize()) {
    fprintf(stderr, "Failed to initialize animated tile renderer.\n");
    return false;
  }

  if (!background_renderer.Initialize(perm_arena, temp_arena, ui_camera.surface_dim)) {
    fprintf(stderr, "Failed to initialize background renderer.\n");
    return false;
  }

  input.SetCallback(OnCharacterPress, this);

  return true;
}

void Game::Update(const InputState& input, float dt) {
  player_manager.Update(input, dt);
  weapon_manager.Update(dt);

  Player* me = player_manager.GetSelf();

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    if (!tile_renderer.CreateMapBuffer(temp_arena, connection.map.filename, ui_camera.surface_dim)) {
      fprintf(stderr, "Failed to create renderable map.\n");
    }

    // TODO: Create new radar when mapzoom changes.
    if (!tile_renderer.CreateRadar(temp_arena, connection.map.filename, ui_camera.surface_dim,
                                   connection.settings.MapZoomFactor)) {
      fprintf(stderr, "Failed to create radar.\n");
    }

    if (me) {
      me->position = Vector2f(512, 512);
    }
  }

  // This must be updated after position update
  specview.Update(input, dt);

  // Cap player and spectator camera to playable area
  if (me) {
    if (me->position.x < 0) me->position.x = 0;
    if (me->position.y < 0) me->position.y = 0;
    if (me->position.x > 1023) me->position.x = 1023;
    if (me->position.y > 1023) me->position.y = 1023;

    camera.position = me->position.PixelRounded();
  }

  render_radar = input.IsDown(InputAction::DisplayMap);
  animated_tile_renderer.Update(dt);
  lvz.Update(dt);
}

void Game::Render(float dt) {
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

  lvz.Render(ui_camera, camera);

  animation.Update(dt);
  background_renderer.Render(camera, sprite_renderer, ui_camera.surface_dim);
  tile_renderer.Render(camera);

  Player* me = player_manager.GetSelf();
  u32 self_freq = 0;
  if (me) {
    self_freq = me->ship < 8 ? me->frequency : specview.spectate_frequency;
  }

  animated_tile_renderer.Render(sprite_renderer, connection.map, camera, ui_camera.surface_dim, flags, flag_count,
                                self_freq);

  if (me) {
    // TODO: Formalize layers
    // Draw animations and weapons before ships and names so they are below
    animation.Render(camera, sprite_renderer);
    weapon_manager.Render(camera, sprite_renderer);

    player_manager.Render(camera, sprite_renderer, self_freq);

    sprite_renderer.Render(camera);

    RenderRadar(me);

    chat.Render(ui_camera, sprite_renderer);

    if (menu_open) {
      RenderMenu();
    }
  }

  // TODO: Move all of this out

  if (connection.login_state == Connection::LoginState::MapDownload) {
    char downloading[64];

    sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                         ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

    int percent = (int)(connection.packet_sequencer.huge_chunks.size * 100 / (float)connection.map.compressed_size);

    sprintf(downloading, "Downloading level: %d%%", percent);
    Vector2f download_pos(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);
    sprite_renderer.DrawText(ui_camera, downloading, TextColor::Blue, download_pos, Layer::TopMost,
                             TextAlignment::Center);
  } else if (connection.login_state < Connection::LoginState::MapDownload) {
    sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                         ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

    Vector2f position(ui_camera.surface_dim.x * 0.5f, (float)(u32)(ui_camera.surface_dim.y * 0.8f));

    sprite_renderer.DrawText(ui_camera, "Entering arena", TextColor::Blue, position, Layer::TopMost,
                             TextAlignment::Center);
  }

  if (me && connection.login_state == Connection::LoginState::Complete) {
    statbox.Render(ui_camera, sprite_renderer);
  }

  char fps_text[32];
  sprintf(fps_text, "FPS: %d", (int)(fps + 0.5f));
  sprite_renderer.DrawText(ui_camera, fps_text, TextColor::Pink, Vector2f(ui_camera.surface_dim.x, 0), Layer::TopMost,
                           TextAlignment::Right);

  sprite_renderer.Render(ui_camera);
}

void Game::RenderRadar(Player* me) {
  if (tile_renderer.radar_renderable.texture != -1 && me) {
    float border = 6.0f;

    if (render_radar) {
      SpriteRenderable& radar_renderable = tile_renderer.radar_renderable;
      Vector2f position = ui_camera.surface_dim - radar_renderable.dimensions - Vector2f(border, border);
      sprite_renderer.Draw(ui_camera, radar_renderable, position, Layer::TopMost);

      Vector2f half_extents = radar_renderable.dimensions * 0.5f;
      Graphics::DrawBorder(sprite_renderer, ui_camera, position + half_extents, half_extents);

      if (sin(GetCurrentTick() / 5) < 0) {
        Vector2f percent = me->position * (1.0f / 1024.0f);
        Vector2f start =
            position + Vector2f(percent.x * radar_renderable.dimensions.x, percent.y * radar_renderable.dimensions.y);

        SpriteRenderable self_renderable = Graphics::color_sprites[25];
        self_renderable.dimensions = Vector2f(2, 2);

        sprite_renderer.Draw(ui_camera, self_renderable, start, Layer::TopMost);
      }
    } else {
      // calculate uvs for displayable map
      SpriteRenderable visible;
      visible.texture = tile_renderer.full_radar_renderable.texture;
      s16 dim = ((((u16)ui_camera.surface_dim.x / 6) / 4) * 8) / 2;
      u16 map_zoom = connection.settings.MapZoomFactor;

      float range = (map_zoom / 48.0f) * 512.0f;
      Vector2f center = me->position;

      // Cap the radar to map range
      if (center.x - range < 0) center.x = range;
      if (center.y - range < 0) center.y = range;
      if (center.x + range > 1024) center.x = 1024 - range;
      if (center.y + range > 1024) center.y = 1024 - range;

      Vector2f min = Vector2f((center.x - range), (center.y - range)).PixelRounded();
      Vector2f max = Vector2f((center.x + range), (center.y + range)).PixelRounded();

      float uv_multiplier = 1.0f / 1024.0f;
      Vector2f min_uv(min.x * uv_multiplier, min.y * uv_multiplier);
      Vector2f max_uv(max.x * uv_multiplier, max.y * uv_multiplier);

      visible.dimensions = Vector2f(dim, dim);
      visible.uvs[0] = Vector2f(min_uv.x, min_uv.y);
      visible.uvs[1] = Vector2f(max_uv.x, min_uv.y);
      visible.uvs[2] = Vector2f(min_uv.x, max_uv.y);
      visible.uvs[3] = Vector2f(max_uv.x, max_uv.y);

      Vector2f position = ui_camera.surface_dim - Vector2f(dim, dim) - Vector2f(border, border);

      sprite_renderer.Draw(ui_camera, visible, position, Layer::TopMost);

      Vector2f half_extents(dim * 0.5f, dim * 0.5f);

      u32 team_freq = me->frequency;

      if (me->ship >= 8) {
        team_freq = specview.spectate_frequency;
      }

      for (size_t i = 0; i < player_manager.player_count; ++i) {
        Player* player = player_manager.players + i;

        if (player->ship >= 8) continue;

        Vector2f p = player->position;
        Vector2f percent((p.x - center.x) * (1.0f / range), (p.y - center.y) * (1.0f / range));

        if (p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y) {
          size_t sprite_index = 34;
          bool render = true;
          Vector2f indicator_dim = player->flags > 0 ? Vector2f(3, 3) : Vector2f(2, 2);

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

          bool is_me = player->id == specview.spectate_id || (player == me && me->ship != 8);

          if (is_me) {
            sprite_index = 29;

            render = sin(GetCurrentTick() / 5) < 0;
          }

          if (render) {
            SpriteRenderable renderable = Graphics::color_sprites[sprite_index];
            Vector2f center_radar = position + half_extents;
            Vector2f start = center_radar + Vector2f(percent.x * half_extents.x, percent.y * half_extents.y);

            renderable.dimensions = indicator_dim;

            sprite_renderer.Draw(ui_camera, renderable, start, Layer::TopMost);
          }
        }
      }

      Graphics::DrawBorder(sprite_renderer, ui_camera, position + half_extents, half_extents);
    }
  }
}

void Game::HandleMenuKey(int codepoint, int mods) {
  switch (codepoint) {
    case 'q':
    case 'Q': {
      struct {
        u8 core;
        u8 type;
      } pkt = {0x00, 0x07};

      connection.Send((u8*)&pkt, sizeof(pkt));
      connection.Disconnect();
    } break;
    default: {
      menu_open = false;
    } break;
  }
}

void Game::RenderMenu() {
  const char* kLeftMenuText[] = {"Q  = Quit",        "F1 = Help",          "F2 = Stat Box",
                                 "F3 = Name tags",   "F4 = Radar",         "F5 = Messages",
                                 "F6 = Help ticker", "F8 = Engine sounds", " A = Arena List",
                                 " B = Set Banner",  " I = Ignore macros", "PgUp/PgDn = Adjust stat box"};

  const char* kRightMenuText[] = {"1 = Warbird", "2 = Javelin",   "3 = Spider", "4 = Leviathan", "5 = Terrier",
                                  "6 = Weasel",  "7 = Lancaster", "8 = Shark",  "S = Spectator"};

  Vector2f dimensions(284.0f, 171.0f);
  Vector2f half_dimensions = dimensions * 0.5f;
  Vector2f topleft((ui_camera.surface_dim.x - dimensions.x) * 0.5f, 3);

  SpriteRenderable background = Graphics::color_sprites[kBackgroundColorIndex];
  background.dimensions = dimensions;

  sprite_renderer.Draw(ui_camera, background, topleft, Layer::TopMost);

  SpriteRenderable separator = Graphics::color_sprites[kSeparatorColorIndex];
  separator.dimensions = Vector2f(dimensions.x, 1);

  sprite_renderer.Draw(ui_camera, separator, topleft + Vector2f(0, 13), Layer::TopMost);
  Graphics::DrawBorder(sprite_renderer, ui_camera, topleft + half_dimensions, half_dimensions);

  sprite_renderer.DrawText(ui_camera, "-= Menu =-", TextColor::Green, Vector2f(topleft.x + half_dimensions.x, 4),
                           Layer::TopMost, TextAlignment::Center);

  float y = 18.0f;

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kLeftMenuText); ++i) {
    sprite_renderer.DrawText(ui_camera, kLeftMenuText[i], TextColor::White, Vector2f(topleft.x + 2, y), Layer::TopMost);
    y += 12.0f;
  }

  sprite_renderer.DrawText(ui_camera, "Any other key to resume game", TextColor::Yellow,
                           Vector2f(topleft.x + half_dimensions.x, y), Layer::TopMost, TextAlignment::Center);

  float right_x = topleft.x + dimensions.x - 13 * 8 - 2;
  y = 18.0f + 12.0f;

  sprite_renderer.DrawText(ui_camera, "Ships", TextColor::DarkRed, Vector2f(right_x + 16.0f, 18.0f), Layer::TopMost);

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kRightMenuText); ++i) {
    sprite_renderer.DrawText(ui_camera, kRightMenuText[i], TextColor::White, Vector2f(right_x, y), Layer::TopMost);
    y += 12.0f;
  }

  sprite_renderer.Render(ui_camera);
}

void Game::OnFlagClaim(u8* pkt, size_t size) {
  u16 id = *(u16*)(pkt + 1);

  assert(id < NULLSPACE_ARRAY_SIZE(flags));

  flags[id].dropped = false;
}

void Game::OnFlagPosition(u8* pkt, size_t size) {
  u16 id = *(u16*)(pkt + 1);
  u16 x = *(u16*)(pkt + 3);
  u16 y = *(u16*)(pkt + 5);
  u16 owner = *(u16*)(pkt + 7);

  assert(id < NULLSPACE_ARRAY_SIZE(flags));

  if (id + 1 > flag_count) {
    flag_count = id + 1;
  }

  flags[id].id = id;
  flags[id].owner = owner;
  flags[id].position = Vector2f((float)x, (float)y);
  flags[id].dropped = true;
}

}  // namespace null
