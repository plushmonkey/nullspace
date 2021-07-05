#include "Game.h"

#include <cassert>
#include <cstdio>

#include "Memory.h"
#include "Platform.h"
#include "Random.h"
#include "Tick.h"
#include "render/Animation.h"
#include "render/Graphics.h"

namespace null {

static void OnCharacterPress(void* user, int codepoint, int mods) {
  Game* game = (Game*)user;

  if (codepoint == NULLSPACE_KEY_ESCAPE) {
    if (game->connection.login_state <= Connection::LoginState::MapDownload ||
        game->connection.login_state >= Connection::LoginState::Quit) {
      game->menu_quit = true;
    }

    game->menu_open = !game->menu_open;
  } else if (game->menu_open) {
    if (game->HandleMenuKey(codepoint, mods)) {
      game->chat.display_full = false;
      return;
    }
  }

  game->chat.OnCharacterPress(codepoint, mods);
}

inline void ToggleStatus(Game* game, Player* self, ShipCapabilityFlags capability, StatusFlag status,
                         AudioType audio_on) {
  if (game->ship_controller.ship.capability & capability) {
    if (self->togglables & status) {
      game->sound_system.Play(AudioType::ToggleOff);
    } else {
      game->sound_system.Play(audio_on);
    }

    self->togglables ^= status;
  }
}

static void OnAction(void* user, InputAction action) {
  Game* game = (Game*)user;
  Player* self = game->player_manager.GetSelf();

  if (!self) return;

  switch (action) {
    case InputAction::Multifire: {
      if (game->ship_controller.ship.capability & ShipCapability_Multifire) {
        if (game->ship_controller.ship.multifire) {
          game->sound_system.Play(AudioType::MultifireOff);
        } else {
          game->sound_system.Play(AudioType::MultifireOn);
        }

        game->ship_controller.ship.multifire = !game->ship_controller.ship.multifire;
      }

    } break;
    case InputAction::Stealth: {
      ToggleStatus(game, self, ShipCapability_Stealth, Status_Stealth, AudioType::Stealth);
    } break;
    case InputAction::Cloak: {
      ToggleStatus(game, self, ShipCapability_Cloak, Status_Cloak, AudioType::Cloak);

      if ((game->ship_controller.ship.capability & ShipCapability_Cloak) && !(self->togglables & Status_Cloak)) {
        self->togglables |= Status_Flash;
      }
    } break;
    case InputAction::XRadar: {
      ToggleStatus(game, self, ShipCapability_XRadar, Status_XRadar, AudioType::XRadar);

      if (self->ship == 8 && !game->connection.settings.NoXRadar) {
        if (self->togglables & Status_XRadar) {
          game->sound_system.Play(AudioType::ToggleOff);
        } else {
          game->sound_system.Play(AudioType::XRadar);
        }

        self->togglables ^= Status_XRadar;
      }
    } break;
    case InputAction::Antiwarp: {
      ToggleStatus(game, self, ShipCapability_Antiwarp, Status_Antiwarp, AudioType::Antiwarp);
    } break;
    case InputAction::Attach: {
      Player* selected = game->statbox.GetSelectedPlayer();

      game->player_manager.AttachSelf(selected);
    } break;
    default: {
    } break;
  }

  game->statbox.OnAction(action);
  game->specview.OnAction(action);
}

static void OnFlagClaimPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagClaim(pkt, size);
}

static void OnFlagPositionPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagPosition(pkt, size);
}

static void OnPlayerIdPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnPlayerId(pkt, size);
}

static void OnArenaSettings(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;
  game->RecreateRadar();
}

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      work_queue(work_queue),
      sound_system(perm_arena),
      notifications(),
      animation(),
      dispatcher(),
      connection(perm_arena, temp_arena, work_queue, dispatcher),
      player_manager(perm_arena, connection, dispatcher),
      weapon_manager(temp_arena, connection, player_manager, dispatcher, animation, sound_system),
      banner_pool(temp_arena, player_manager, dispatcher),
      camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f / 16.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(60.0f),
      statbox(player_manager, banner_pool, dispatcher),
      chat(dispatcher, connection, player_manager, statbox),
      specview(connection, statbox),
      ship_controller(player_manager, weapon_manager, dispatcher, notifications),
      lvz(perm_arena, temp_arena, connection.requester, sprite_renderer, dispatcher),
      radar(player_manager) {
  float zmax = (float)Layer::Count;
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -zmax, zmax);
  dispatcher.Register(ProtocolS2C::FlagPosition, OnFlagPositionPkt, this);
  dispatcher.Register(ProtocolS2C::FlagClaim, OnFlagClaimPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerId, OnPlayerIdPkt, this);
  dispatcher.Register(ProtocolS2C::ArenaSettings, OnArenaSettings, this);

  player_manager.Initialize(&weapon_manager, &ship_controller, &chat, &notifications, &specview, &banner_pool);
  weapon_manager.Initialize(&ship_controller, &radar);

  connection.view_dim = ui_camera.surface_dim;
}

bool Game::Initialize(InputState& input) {
  if (g_Settings.sound_enabled && !sound_system.Initialize()) {
    log_error("Failed to initialize sound system.\n");
  }

  if (!sprite_renderer.Initialize(perm_arena)) {
    log_error("Failed to initialize sprite renderer.\n");
    return false;
  }

  if (!Graphics::Initialize(sprite_renderer)) {
    log_error("Failed to initialize graphics.\n");
    return false;
  }

  if (!background_renderer.Initialize(perm_arena, temp_arena, ui_camera.surface_dim)) {
    log_error("Failed to initialize background renderer.\n");
    return false;
  }

  input.SetCallback(OnCharacterPress, this);
  input.action_callback = OnAction;

  return true;
}

bool Game::Update(const InputState& input, float dt) {
  chat.display_full = menu_open;

  Graphics::colors.Update(dt);

  connection.map.UpdateDoors(connection.settings);

  player_manager.Update(dt);
  ship_controller.Update(input, dt);
  weapon_manager.Update(dt);

  Player* me = player_manager.GetSelf();

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    if (!tile_renderer.CreateMapBuffer(temp_arena, connection.map.filename, ui_camera.surface_dim)) {
      log_error("Failed to create renderable map.\n");
    }

    mapzoom = connection.settings.MapZoomFactor;

    if (!tile_renderer.CreateRadar(temp_arena, connection.map.filename, ui_camera.surface_dim,
                                   connection.settings.MapZoomFactor)) {
      log_error("Failed to create radar.\n");
    }

    animated_tile_renderer.InitializeDoors(tile_renderer);

    if (me) {
      me->position = Vector2f(0, 0);

      for (size_t i = 0; i < player_manager.player_count; ++i) {
        Player* player = player_manager.GetPlayerById(statbox.player_view[i]);

        if (player->ship != 8) {
          specview.SpectatePlayer(*player);
          break;
        }
      }
    }
  }

  // This must be updated after position update
  specview.Update(input, dt);

  // Cap player and spectator camera to playable area
  if (me) {
    if (me->position.x < 0) me->position.x = 0;
    if (me->position.y < 0) me->position.y = 0;
    if (me->position.x >= 1024) me->position.x = 1023.9f;
    if (me->position.y >= 1024) me->position.y = 1023.9f;

    camera.position = me->position.PixelRounded();
  }

  render_radar = input.IsDown(InputAction::DisplayMap);
  animated_tile_renderer.Update(dt);
  lvz.Update(dt);

  UpdateGreens(dt);

  u32 tick = GetCurrentTick();

  // TODO: Spatial partition queries
  for (size_t i = 0; i < flag_count; ++i) {
    GameFlag* flag = flags + i;

    if (TICK_GT(flag->hidden_end_tick, tick)) continue;

    Vector2f flag_min = flag->position;
    Vector2f flag_max = flag->position + Vector2f(1, 1);

    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* player = player_manager.players + i;

      if (player->ship == 8) continue;
      if (player->enter_delay > 0.0f) continue;
      if (player->frequency == flag->owner) continue;

      float radius = connection.settings.ShipSettings[player->ship].GetRadius();
      Vector2f player_min = player->position - Vector2f(radius, radius);
      Vector2f player_max = player->position + Vector2f(radius, radius);

      if (BoxBoxIntersect(flag_min, flag_max, player_min, player_max)) {
        constexpr u32 kHideFlagDelay = 300;
        flag->hidden_end_tick = tick + kHideFlagDelay;

        if (player->id == player_manager.player_id) {
          // Send flag pickup
          connection.SendFlagRequest(flag->id);
        }
      }
    }
  }

  radar.Update(ui_camera, connection.settings.MapZoomFactor, specview.GetFrequency(), specview.spectate_id);

  return !menu_quit;
}

void Game::UpdateGreens(float dt) {
  u32 tick = GetCurrentTick();

  for (size_t i = 0; i < green_count; ++i) {
    PrizeGreen* green = greens + i;

    if (tick >= green->end_tick) {
      greens[i--] = greens[--green_count];
    }
  }

  if (TICK_GT(tick, last_green_collision_tick)) {
    Player* self = player_manager.GetSelf();

    // TODO: Should probably speed this up with an acceleration structure. It's only done once a tick so it's not
    // really important to speed up.
    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* player = player_manager.players + i;

      if (player->ship != 8) {
        float radius = connection.settings.ShipSettings[player->ship].GetRadius();

        Vector2f pmin = player->position - Vector2f(radius, radius);
        Vector2f pmax = player->position + Vector2f(radius, radius);

        for (size_t i = 0; i < green_count; ++i) {
          PrizeGreen* green = greens + i;

          Vector2f gmin = green->position;
          Vector2f gmax = gmin + Vector2f(1, 1);

          if (green->end_tick > 0 && BoxBoxIntersect(pmin, pmax, gmin, gmax)) {
            if (player == self) {
              // Pick up green
              sound_system.Play(AudioType::Prize);
              ship_controller.ApplyPrize(self, green->prize_id, true);
              connection.SendTakeGreen((u16)green->position.x, (u16)green->position.y, green->prize_id);
            }

            // Set the end tick to zero so it gets automatically removed next update
            green->end_tick = 0;
          }
        }
      }
    }

    last_green_collision_tick = tick;
  }

  if (connection.security.prize_seed == 0) return;

  s32 tick_count = TICK_DIFF(tick, last_green_tick);

  if (tick_count < connection.settings.PrizeDelay) return;

  s32 update_count = 1;

  if (connection.settings.PrizeDelay > 0) {
    update_count = tick_count / connection.settings.PrizeDelay;
  }

  last_green_tick = tick;

  size_t max_greens = (connection.settings.PrizeFactor * player_manager.player_count) / 1000;
  if (max_greens > NULLSPACE_ARRAY_SIZE(greens)) {
    max_greens = NULLSPACE_ARRAY_SIZE(greens);
  }

  u16 spawn_extent =
      connection.settings.MinimumVirtual + connection.settings.UpgradeVirtual * (u16)player_manager.player_count;
  if (spawn_extent < 3) {
    spawn_extent = 3;
  } else if (spawn_extent > 1024) {
    spawn_extent = 1024;
  }

  for (s32 i = 0; i < update_count; ++i) {
    VieRNG rng = {(s32)connection.security.prize_seed};

    for (s32 j = 0; j < connection.settings.PrizeHideCount; ++j) {
      u16 x = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));
      u16 y = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));

      s32 prize_id = ship_controller.GeneratePrize(true);

      u32 duration_rng = rng.GetNext();

      // Insert prize if it's valid and in an empty map space
      if (prize_id != 0 && green_count < max_greens && connection.map.GetTileId(x, y) == 0) {
        PrizeGreen* green = greens + green_count++;

        green->position.x = (float)x;
        green->position.y = (float)y;
        green->prize_id = prize_id;

        s16 exist_diff = (connection.settings.PrizeMaxExist - connection.settings.PrizeMinExist);

        u32 duration = (duration_rng % (exist_diff + 1)) + connection.settings.PrizeMinExist;
        green->end_tick = tick + duration;
      }
    }

    connection.security.prize_seed = rng.seed;
  }
}

void Game::Render(float dt) {
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

  lvz.Render(ui_camera, camera);

  animation.Update(dt);

  if (connection.login_state == Connection::LoginState::Complete) {
    RenderGame(dt);
  } else {
    RenderJoin(dt);
  }

  char fps_text[32];
  sprintf(fps_text, "FPS: %d", (int)(fps + 0.5f));
  sprite_renderer.DrawText(ui_camera, fps_text, TextColor::Pink, Vector2f(ui_camera.surface_dim.x, 24), Layer::TopMost,
                           TextAlignment::Right);

  sprite_renderer.Render(ui_camera);
}

void Game::RenderGame(float dt) {
  background_renderer.Render(camera, sprite_renderer, ui_camera.surface_dim);
  tile_renderer.Render(camera);

  Player* self = player_manager.GetSelf();
  u32 self_freq = specview.GetFrequency();

  animated_tile_renderer.Render(sprite_renderer, connection.map, camera, ui_camera.surface_dim, flags, flag_count,
                                greens, green_count, self_freq);

  if (self) {
    animation.Render(camera, sprite_renderer);

    u8 visibility_ship = specview.GetVisibilityShip();

    RadarVisibility radar_visibility;
    if (visibility_ship != 8) {
      radar_visibility.see_mines = connection.settings.ShipSettings[visibility_ship].SeeMines;
      radar_visibility.see_bomb_level = connection.settings.ShipSettings[visibility_ship].SeeBombLevel;
    } else {
      radar_visibility.see_mines = false;
      radar_visibility.see_bomb_level = 0;
    }

    weapon_manager.Render(camera, ui_camera, sprite_renderer, dt, radar_visibility);
    player_manager.Render(camera, sprite_renderer);

    sprite_renderer.Render(camera);

    ship_controller.Render(ui_camera, camera, sprite_renderer);

    sprite_renderer.Render(camera);

    for (size_t i = 0; i < flag_count; ++i) {
      GameFlag* flag = flags + i;

      if (flag->owner == specview.GetFrequency()) {
        radar.AddTemporaryIndicator(flag->position, 0, Vector2f(2, 2), ColorType::RadarTeamFlag);
      }
    }

    if (render_radar) {
      radar.RenderFull(ui_camera, sprite_renderer, tile_renderer);
    } else {
      radar.Render(ui_camera, sprite_renderer, tile_renderer, connection.settings.MapZoomFactor, greens, green_count);
    }

    chat.Render(ui_camera, sprite_renderer);
    notifications.Render(ui_camera, sprite_renderer);

    if (menu_open) {
      RenderMenu();
    }
  }

  statbox.Render(ui_camera, sprite_renderer);
  specview.Render(ui_camera, sprite_renderer);
  sprite_renderer.Render(ui_camera);
}

void Game::RenderJoin(float dt) {
  // TODO: Moving stars during load

  sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                       ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

  switch (connection.login_state) {
    case Connection::LoginState::EncryptionRequested:
    case Connection::LoginState::Authentication:
    case Connection::LoginState::Registering:
    case Connection::LoginState::ArenaLogin: {
      sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                           ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

      Vector2f position(ui_camera.surface_dim.x * 0.5f, (float)(u32)(ui_camera.surface_dim.y * 0.8f));

      if (connection.packets_received > 0) {
        sprite_renderer.DrawText(ui_camera, "Entering arena", TextColor::Blue, position, Layer::TopMost,
                                 TextAlignment::Center);
      } else {
        sprite_renderer.DrawText(ui_camera, "Connecting to server", TextColor::Blue, position, Layer::TopMost,
                                 TextAlignment::Center);
      }
    } break;
    case Connection::LoginState::MapDownload: {
      int percent = (int)(connection.packet_sequencer.huge_chunks.size * 100 / (float)connection.map.compressed_size);
      char downloading[64];

      sprintf(downloading, "Downloading level: %d%%", percent);

      Vector2f download_pos(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);

      sprite_renderer.DrawText(ui_camera, downloading, TextColor::Blue, download_pos, Layer::TopMost,
                               TextAlignment::Center);
      statbox.Render(ui_camera, sprite_renderer);
      sprite_renderer.Render(ui_camera);
    } break;
    case Connection::LoginState::Quit:
    case Connection::LoginState::ConnectTimeout: {
      sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                           ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

      Vector2f position(ui_camera.surface_dim.x * 0.5f, (float)(u32)(ui_camera.surface_dim.y * 0.8f));

      sprite_renderer.DrawText(ui_camera, "Failed to connect to server", TextColor::DarkRed, position, Layer::TopMost,
                               TextAlignment::Center);
    } break;
    default: {
    } break;
  }
}

bool Game::HandleMenuKey(int codepoint, int mods) {
  bool handled = false;

  switch (codepoint) {
    case 'q':
    case 'Q': {
      connection.SendDisconnect();
      connection.Disconnect();
      menu_quit = true;
      handled = true;
    } break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8': {
      int ship = codepoint - '1';
      Player* self = player_manager.GetSelf();

      if (self && self->ship != ship) {
        printf("Sending ship request for %d\n", ship + 1);
        connection.SendShipRequest((u8)ship);
      }
      handled = true;
    } break;
    case 's':
    case 'S': {
      Player* self = player_manager.GetSelf();

      if (self && self->ship != 8) {
        printf("Sending spectate request.\n");

        connection.SendShipRequest(8);
      }
      handled = true;
    } break;
    default: {
    } break;
  }

  menu_open = false;
  return handled;
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

  SpriteRenderable background = Graphics::GetColor(ColorType::Background, dimensions);
  sprite_renderer.Draw(ui_camera, background, topleft, Layer::TopMost);

  SpriteRenderable separator = Graphics::GetColor(ColorType::Border1, Vector2f(dimensions.x, 1));

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

void Game::RecreateRadar() {
  if (connection.login_state != Connection::LoginState::Complete || connection.settings.MapZoomFactor == mapzoom) {
    return;
  }

  mapzoom = connection.settings.MapZoomFactor;

  if (!tile_renderer.CreateRadar(temp_arena, connection.map.filename, ui_camera.surface_dim, mapzoom)) {
    fprintf(stderr, "Failed to create radar.\n");
  }
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

  if (id + 1 > (u16)flag_count) {
    flag_count = id + 1;
  }

  flags[id].id = id;
  flags[id].owner = owner;
  flags[id].position = Vector2f((float)x, (float)y);
  flags[id].dropped = true;
  flags[id].hidden_end_tick = 0;
}

void Game::OnPlayerId(u8* pkt, size_t size) {
  Cleanup();

  // TODO: Handle and display errors

  last_green_tick = GetCurrentTick();

  lvz.Reset();

  if (g_Settings.sound_enabled && !sound_system.Initialize()) {
    log_error("Failed to initialize sound system.\n");
  }

  if (!sprite_renderer.Initialize(perm_arena)) {
    fprintf(stderr, "Failed to initialize sprite renderer.\n");
    exit(1);
  }

  if (!tile_renderer.Initialize()) {
    fprintf(stderr, "Failed to initialize tile renderer.\n");
    exit(1);
  }

  if (!Graphics::Initialize(sprite_renderer)) {
    fprintf(stderr, "Failed to initialize graphics.\n");
    exit(1);
  }

  animated_tile_renderer.Initialize();

  if (!background_renderer.Initialize(perm_arena, temp_arena, ui_camera.surface_dim)) {
    fprintf(stderr, "Failed to initialize background renderer.\n");
    exit(1);
  }
}

void Game::Cleanup() {
  work_queue.Clear();
  sound_system.Cleanup();
  banner_pool.Cleanup();
  background_renderer.Cleanup();
  sprite_renderer.Cleanup();
  tile_renderer.Cleanup();
}

}  // namespace null
