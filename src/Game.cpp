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
  if (game->specview.OnAction(action)) {
    game->RecreateRadar();
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

static void OnPlayerIdPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnPlayerId(pkt, size);
}

static void OnArenaSettingsPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;
  game->RecreateRadar();
}

static void OnTurfFlagUpdatePkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnTurfFlagUpdate(pkt, size);
}

static void OnPlayerFreqAndShipChangePkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 ship = buffer.ReadU8();
  u16 pid = buffer.ReadU16();
  u16 freq = buffer.ReadU16();

  u16 spectate_id = game->specview.GetPlayerId();
  if (pid == spectate_id || pid == game->player_manager.player_id) {
    Player* player = game->player_manager.GetPlayerById(pid);

    if (player) {
      // Force update the frequency so the radar colors will change correctly.
      player->frequency = freq;

      if (pid == spectate_id && ship != 8) {
        game->specview.spectate_frequency = freq;
      }
    }

    game->RecreateRadar();
  }
}

static void OnPlayerDeathPkt(void* user, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 green_id = buffer.ReadU8();
  u16 killer_id = buffer.ReadU16();
  u16 killed_id = buffer.ReadU16();

  Game* game = (Game*)user;

  Player* killed = game->player_manager.GetPlayerById(killed_id);
  Player* killer = game->player_manager.GetPlayerById(killer_id);

  // Only spawn greens if they are positive and the killed player has moved.
  if (green_id > 0 && killer && killed && killed->velocity != Vector2f(0, 0)) {
    game->SpawnDeathGreen(killed->position, (Prize)green_id);
  }
}

static void OnSecurityRequestPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  // Reset green timer so it can synchronize with other clients
  game->green_ticks = 0;
  game->last_green_tick = GetCurrentTick();
  game->last_green_collision_tick = GetCurrentTick();
}

static void OnPlayerPrizePkt(void* user, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);
  Game* game = (Game*)user;

  buffer.ReadU8();   // type
  buffer.ReadU32();  // timestamp

  u16 x = buffer.ReadU16();
  u16 y = buffer.ReadU16();
  u16 prize_id = buffer.ReadU16();
  u16 player_id = buffer.ReadU16();

  // Loop through greens to remove any on that tile. This exists so players outside of position broadcast range will
  // still keep prize seed in sync.
  for (size_t i = 0; i < game->green_count; ++i) {
    PrizeGreen* green = game->greens + i;

    u16 green_x = (u16)green->position.x;
    u16 green_y = (u16)green->position.y;

    if (green_x == x && green_y == y) {
      game->greens[i] = game->greens[--game->green_count];
      break;
    }
  }

  // Perform prize sharing
  Player* player = game->player_manager.GetPlayerById(player_id);
  if (player && player->id != game->player_manager.player_id) {
    Player* self = game->player_manager.GetSelf();

    if (self && self->ship != 8 && self->frequency == player->frequency) {
      u16 share_limit = game->connection.settings.ShipSettings[self->ship].PrizeShareLimit;

      if (self->bounty < share_limit) {
        game->ship_controller.ApplyPrize(self, prize_id, true);
      }
    }
  }
}

static void OnBombDamageTaken(void* user) {
  Game* game = (Game*)user;

  game->jitter_time = game->connection.settings.JitterTime / 100.0f;
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
      player_manager(perm_arena, connection, dispatcher, sound_system),
      weapon_manager(temp_arena, connection, player_manager, dispatcher, animation, sound_system),
      brick_manager(perm_arena, connection, dispatcher),
      banner_pool(temp_arena, player_manager, dispatcher),
      camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f / 16.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(60.0f),
      statbox(player_manager, banner_pool, dispatcher),
      chat(dispatcher, connection, player_manager, statbox),
      specview(connection, statbox),
      soccer(connection, specview),
      ship_controller(player_manager, weapon_manager, dispatcher, notifications),
      lvz(perm_arena, temp_arena, connection.requester, sprite_renderer, dispatcher),
      radar(player_manager) {
  float zmax = (float)Layer::Count;
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -zmax, zmax);
  dispatcher.Register(ProtocolS2C::FlagPosition, OnFlagPositionPkt, this);
  dispatcher.Register(ProtocolS2C::FlagClaim, OnFlagClaimPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerId, OnPlayerIdPkt, this);
  dispatcher.Register(ProtocolS2C::ArenaSettings, OnArenaSettingsPkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::TurfFlagUpdate, OnTurfFlagUpdatePkt, this);
  dispatcher.Register(ProtocolS2C::PlayerDeath, OnPlayerDeathPkt, this);
  dispatcher.Register(ProtocolS2C::Security, OnSecurityRequestPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerPrize, OnPlayerPrizePkt, this);

  player_manager.Initialize(&weapon_manager, &ship_controller, &chat, &notifications, &specview, &banner_pool, &radar);
  weapon_manager.Initialize(&ship_controller, &radar);

  connection.view_dim = ui_camera.surface_dim;

  ship_controller.explosion_report.on_damage = OnBombDamageTaken;
  ship_controller.explosion_report.user = this;
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

  chat.CreateCursor(sprite_renderer);

  if (g_Settings.render_stars && !background_renderer.Initialize(perm_arena, temp_arena, ui_camera.surface_dim)) {
    log_error("Failed to initialize background renderer.\n");
    return false;
  }

  input.SetCallback(OnCharacterPress, this);
  input.action_callback = OnAction;

  return true;
}

bool Game::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  chat.display_full = menu_open;

  Graphics::colors.Update(dt);

  connection.map.UpdateDoors(connection.settings);

  player_manager.Update(dt);
  ship_controller.Update(input, dt);
  weapon_manager.Update(dt);

  chat.Update(dt);

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    if (self) {
      if (self->ship == 8) {
        self->position = Vector2f(0, 0);

        for (size_t i = 0; i < player_manager.player_count; ++i) {
          Player* player = player_manager.GetPlayerById(statbox.player_view[i]);

          if (player->ship != 8) {
            specview.SpectatePlayer(*player);
            break;
          }
        }
      } else {
        player_manager.Spawn(true);
      }
    }

    if (!tile_renderer.CreateMapBuffer(temp_arena, connection.map.filename, ui_camera.surface_dim)) {
      log_error("Failed to create renderable map.\n");
    }

    mapzoom = connection.settings.MapZoomFactor;

    if (!tile_renderer.CreateRadar(temp_arena, connection.map, ui_camera.surface_dim, connection.settings.MapZoomFactor,
                                   soccer)) {
      log_error("Failed to create radar.\n");
    }

    animated_tile_renderer.InitializeDoors(tile_renderer);
    connection.map.brick_manager = &brick_manager;
  }

  // This must be updated after position update
  if (specview.Update(input, dt)) {
    RecreateRadar();
  }

  brick_manager.Update(connection.map, specview.GetFrequency(), dt);

  // Cap player and spectator camera to playable area
  if (self) {
    if (self->position.x < 0) self->position.x = 0;
    if (self->position.y < 0) self->position.y = 0;
    if (self->position.x >= 1024) self->position.x = 1023.9f;
    if (self->position.y >= 1024) self->position.y = 1023.9f;

    camera.position = self->position.PixelRounded();

    if (jitter_time > 0) {
      float max_jitter_time = connection.settings.JitterTime / 100.0f;
      float strength = jitter_time / max_jitter_time;
      float max_jitter_distance = max_jitter_time;

      if (max_jitter_distance > 2.0f) {
        max_jitter_distance = 2.0f;
      }

      camera.position.x += sin(GetCurrentTick() * 0.75f) * strength * max_jitter_distance;
      camera.position.y += sin(GetCurrentTick() * 0.63f) * strength * max_jitter_distance;

      jitter_time -= dt;
    }
  }

  render_radar = input.IsDown(InputAction::DisplayMap);
  animated_tile_renderer.Update(dt);
  lvz.Update(dt);

  radar.Update(ui_camera, connection.settings.MapZoomFactor, specview.GetFrequency(), specview.spectate_id);

  UpdateGreens(dt);

  u32 tick = GetCurrentTick();

  // TODO: Spatial partition queries
  for (size_t i = 0; i < flag_count; ++i) {
    GameFlag* flag = flags + i;

    if (TICK_GT(flag->hidden_end_tick, tick)) continue;

    Vector2f flag_min = flag->position;
    Vector2f flag_max = flag->position + Vector2f(1, 1);

    for (size_t j = 0; j < player_manager.player_count; ++j) {
      Player* player = player_manager.players + j;

      if (player->ship == 8) continue;
      if (player->enter_delay > 0.0f) continue;
      if (player->frequency == flag->owner) continue;
      if (!player_manager.IsSynchronized(*player)) continue;

      float radius = connection.settings.ShipSettings[player->ship].GetRadius();
      Vector2f player_min = player->position - Vector2f(radius, radius);
      Vector2f player_max = player->position + Vector2f(radius, radius);

      if (BoxBoxIntersect(flag_min, flag_max, player_min, player_max)) {
        constexpr u32 kHideFlagDelay = 300;

        if (!(flag->flags & GameFlag_Turf)) {
          flag->hidden_end_tick = tick + kHideFlagDelay;
        }

        u32 carry = connection.settings.CarryFlags;
        bool can_carry = carry > 0 && (carry == 1 || player->flags < carry - 1);

        u32 view_tick = connection.login_tick + connection.settings.EnterGameFlaggingDelay;

        if (TICK_GT(tick, view_tick) && (can_carry || (flag->flags & GameFlag_Turf))) {
          if (player->id == player_manager.player_id &&
              TICK_DIFF(tick, flag->last_pickup_request_tick) >= kFlagPickupDelay) {
            // Send flag pickup
            connection.SendFlagRequest(flag->id);
            flag->last_pickup_request_tick = tick;
          }
        }
      }
    }
  }

  if (self) {
    s32 tick_diff = TICK_DIFF(tick, last_tick);

    if (self->flag_timer > 0 && tick_diff > 0) {
      s32 new_timer = self->flag_timer - tick_diff;

      if (new_timer <= 0) {
        connection.SendFlagDrop();
        self->flag_timer = 0;
      } else {
        self->flag_timer = (u16)new_timer;
      }
    }

    last_tick = GetCurrentTick();
  }

  return !menu_quit;
}

void Game::UpdateGreens(float dt) {
  u32 tick = GetCurrentTick();

  if (TICK_GT(tick, last_green_collision_tick)) {
    Player* self = player_manager.GetSelf();

    // TODO: Should probably speed this up with an acceleration structure. It's only done once a tick so it's not
    // really important to speed up.
    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* player = player_manager.players + i;

      if (player->ship == 8) continue;
      if (player->enter_delay > 0) continue;
      if (!player_manager.IsSynchronized(*player)) continue;

      float radius = connection.settings.ShipSettings[player->ship].GetRadius();

      Vector2f pmin = player->position - Vector2f(radius, radius);
      Vector2f pmax = player->position + Vector2f(radius, radius);

      for (size_t j = 0; j < green_count; ++j) {
        PrizeGreen* green = greens + j;

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

    last_green_collision_tick = tick;
  }

  for (size_t i = 0; i < green_count; ++i) {
    PrizeGreen* green = greens + i;

    if (tick > green->end_tick) {
      greens[i--] = greens[--green_count];
    }
  }

  if (connection.security.prize_seed == 0) return;

  s32 tick_count = TICK_DIFF(tick, last_green_tick);
  if (tick_count <= 0) return;

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

  for (s32 i = 0; i < tick_count; ++i) {
    if (++green_ticks >= (u32)connection.settings.PrizeDelay) {
      for (s32 j = 0; j < connection.settings.PrizeHideCount; ++j) {
        VieRNG rng = {(s32)connection.security.prize_seed};

        u16 x = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));
        u16 y = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));

        connection.security.prize_seed = rng.seed;

        s32 prize_id = ship_controller.GeneratePrize(true);

        rng.seed = (s32)connection.security.prize_seed;

        u32 duration_rng = rng.GetNext();

        connection.security.prize_seed = rng.seed;

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

      green_ticks = 0;
    }

    last_green_tick = tick;
  }
}

void Game::SpawnDeathGreen(const Vector2f& position, Prize prize) {
  if (green_count >= kMaxGreenCount) return;

  PrizeGreen* green = greens + green_count++;

  green->position = position;
  green->prize_id = (s32)prize;
  green->end_tick = GetCurrentTick() + connection.settings.DeathPrizeTime;
}

void Game::Render(float dt) {
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

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
  lvz.Render(ui_camera, camera);

  if (g_Settings.render_stars) {
    background_renderer.Render(camera, sprite_renderer, ui_camera.surface_dim);
  }

  tile_renderer.Render(camera);

  Player* self = player_manager.GetSelf();
  u32 self_freq = specview.GetFrequency();

  size_t viewable_flag_count = flag_count;
  u32 tick = GetCurrentTick();
  bool hide_spec_flags = self && self->ship == 8 && connection.settings.HideFlags;

  if (hide_spec_flags || !TICK_GT(tick, connection.login_tick + connection.settings.EnterGameFlaggingDelay)) {
    viewable_flag_count = 0;
  }

  animated_tile_renderer.Render(sprite_renderer, connection.map, camera, ui_camera.surface_dim, flags,
                                viewable_flag_count, greens, green_count, self_freq, soccer);
  brick_manager.Render(camera, sprite_renderer, ui_camera.surface_dim, self_freq);

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
  if (connection.login_state != Connection::LoginState::Complete) {
    return;
  }

  mapzoom = connection.settings.MapZoomFactor;

  if (!tile_renderer.CreateRadar(temp_arena, connection.map, ui_camera.surface_dim, mapzoom, soccer)) {
    fprintf(stderr, "Failed to create radar.\n");
  }
}

void Game::OnFlagClaim(u8* pkt, size_t size) {
  u16 id = *(u16*)(pkt + 1);
  u16 player_id = *(u16*)(pkt + 3);

  assert(id < NULLSPACE_ARRAY_SIZE(flags));

  Player* player = player_manager.GetPlayerById(player_id);

  if (!player) return;

  if (!(flags[id].flags & GameFlag_Turf)) {
    bool was_dropped = flags[id].flags & GameFlag_Dropped;

    flags[id].flags &= ~GameFlag_Dropped;

    player->flags++;

    if (was_dropped && player->id == specview.GetPlayerId()) {
      player->flag_timer = connection.settings.FlagDropDelay;
      sound_system.Play(AudioType::Flag);
    }
  } else {
    flags[id].owner = player->frequency;

    if (player->id == specview.GetPlayerId()) {
      sound_system.Play(AudioType::Flag);
    }
  }
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
  flags[id].flags |= GameFlag_Dropped;
  flags[id].hidden_end_tick = 0;
}

void Game::OnTurfFlagUpdate(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  if (connection.login_state != Connection::LoginState::Complete) return;

  u16 id = 0;
  while (buffer.read < buffer.write) {
    u16 team = buffer.ReadU16();

    AnimatedTileSet& tileset = connection.map.GetAnimatedTileSet(AnimatedTile::Flag);

    assert(id < tileset.count);

    Tile* tile = &tileset.tiles[id];

    if (id + 1 > (u16)flag_count) {
      flag_count = id + 1;
    }

    flags[id].id = id;
    flags[id].owner = team;
    flags[id].position = Vector2f((float)tile->x, (float)tile->y);
    flags[id].flags |= GameFlag_Dropped | GameFlag_Turf;
    flags[id].hidden_end_tick = 0;

    ++id;
  }
}

void Game::OnPlayerId(u8* pkt, size_t size) {
  Cleanup();

  // TODO: Handle and display errors

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

  chat.CreateCursor(sprite_renderer);

  animated_tile_renderer.Initialize();

  if (!background_renderer.Initialize(perm_arena, temp_arena, ui_camera.surface_dim)) {
    fprintf(stderr, "Failed to initialize background renderer.\n");
    exit(1);
  }
}

void Game::Cleanup() {
  brick_manager.Clear();
  work_queue.Clear();
  sound_system.Cleanup();
  banner_pool.Cleanup();
  background_renderer.Cleanup();
  sprite_renderer.Cleanup();
  tile_renderer.Cleanup();
}

}  // namespace null
