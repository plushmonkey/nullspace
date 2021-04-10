#include "Game.h"

#include <cstdio>

#include "Memory.h"
#include "Tick.h"

namespace null {

#define SIM_TEST 0

void Simulate(Connection& connection, float dt);

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      connection(perm_arena, temp_arena),
      camera(Vector2f((float)width, (float)height), Vector2f(512, 512), 1.0f / 16.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(0.0f) {
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -1.0f, 1.0f);
}

bool Game::Initialize() {
  if (!tile_renderer.Initialize()) {
    return false;
  }

  if (!sprite_renderer.Initialize(perm_arena)) {
    return false;
  }

  int count;
  ship_sprites = sprite_renderer.LoadSheet("graphics/ships.bm2", Vector2f(36, 36), &count);
  spectate_sprites = sprite_renderer.LoadSheet("graphics/spectate.bm2", Vector2f(8, 8), &count);

  return true;
}

void Game::Update(float dt) {
  static const Vector2f kBasePosition(512.0f, 512.f);
  static float timer = 0.0f;

  timer += dt * 0.0375f;
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

  Simulate(connection, dt);

  Player* me = connection.GetPlayerById(connection.player_id);
  if (me) {
    me->position = Vector2f(512 + 35, 512 + 35);
    camera.position = me->position;
  }

  if (me && me->ship != 8) {
    camera.position = me->position;
  }

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    tile_renderer.CreateMapBuffer(temp_arena, connection.map_handler.filename);
  }
}

void Game::Render() {
  Player* me = connection.GetPlayerById(connection.player_id);

  for (size_t i = 0; i < connection.player_count; ++i) {
    Player* player = connection.players + i;

    if (player->ship == 8) continue;

    size_t index = player->ship * 40 + player->direction;
    float radius = connection.settings.ShipSettings[player->ship].Radius / 16.0f;
    if (radius == 0.0f) {
      radius = 14.0f / 16.0f;
    }

    sprite_renderer.Draw(camera, ship_sprites[index], player->position - Vector2f(radius, radius));
    radius += 2.0f / 16.0f;

    char display[32];
    sprintf(display, "%s(%d)[%d]", player->name, player->bounty, player->ping * 10);

    if (me) {
      TextColor color = me->frequency == player->frequency ? TextColor::Yellow : TextColor::Blue;
      sprite_renderer.DrawText(camera, display, color, player->position + Vector2f(radius, radius));
    }
  }

  tile_renderer.Render(camera);
  sprite_renderer.Render(camera);

  // TODO: Move all of this out

  if (connection.login_state == Connection::LoginState::MapDownload) {
    char downloading[64];

    sprite_renderer.Draw(ui_camera, ship_sprites[0],
                         ui_camera.surface_dim * 0.5f - Vector2f(14.0f / 16.0f, 14.0f / 16.0f));

    int percent =
        (int)(connection.packet_sequencer.huge_chunks.size * 100 / (float)connection.map_handler.compressed_size);

    sprintf(downloading, "Downloading level: %d%%", percent);
    Vector2f download_pos(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);
    sprite_renderer.DrawText(ui_camera, downloading, TextColor::Blue, download_pos, TextAlignment::Center);
  }

  if (me && connection.login_state == Connection::LoginState::Complete) {
    char count_text[16];
    sprintf(count_text, "     %zd", connection.player_count);

    sprite_renderer.DrawText(ui_camera, count_text, TextColor::Green, Vector2f(0, 0));

    float offset_y = 24.0f;

    for (size_t i = 0; i < connection.player_count; ++i) {
      Player* player = connection.players + i;
      if (player->frequency != me->frequency) continue;

      float render_y = offset_y;
      if (player->id == me->id) {
        render_y = 12.0f;
      } else {
        offset_y += 12.0f;
      }

      if (player->ship == 8) {
        size_t index = player->id == me->id ? 2 : 1;
        sprite_renderer.Draw(ui_camera, spectate_sprites[index], Vector2f(2, render_y + 3.0f));
      }

      sprite_renderer.DrawText(ui_camera, player->name, TextColor::Yellow, Vector2f(12, render_y));
    }

    for (size_t i = 0; i < connection.player_count; ++i) {
      Player* player = connection.players + i;
      if (player->frequency == me->frequency) continue;

      if (player->ship == 8) {
        sprite_renderer.Draw(ui_camera, spectate_sprites[1], Vector2f(2, offset_y + 3.0f));
      }

      sprite_renderer.DrawText(ui_camera, player->name, TextColor::White, Vector2f(12, offset_y));
      offset_y += 12.0f;
    }
  }

  char fps_text[32];
  sprintf(fps_text, "FPS: %d", (int)(fps + 0.5f));
  sprite_renderer.DrawText(ui_camera, fps_text, TextColor::Pink, Vector2f(ui_camera.surface_dim.x, 0),
                           TextAlignment::Right);

  sprite_renderer.Render(ui_camera);
}

// Test fun code
void Simulate(Connection& connection, float dt) {
  static int last_request = 0;

  if (connection.login_state != Connection::LoginState::Complete) return;

  for (size_t i = 0; i < connection.player_count; ++i) {
    Player* player = connection.players + i;

    if (player->ship == 8) continue;

    player->position += player->velocity * dt;
    if (player->lerp_time > 0.0f) {
      float timestep = dt;
      if (player->lerp_time < timestep) {
        timestep = player->lerp_time;
      }
      player->position += player->lerp_velocity * timestep;
      player->lerp_time -= timestep;
    }
  }

  Player* player = connection.GetPlayerById(connection.player_id);
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
    waypoint_index = (waypoint_index + 1) % (sizeof(waypoints) / sizeof(*waypoints));
  }
#else
  player->position = Vector2f(512, 512);
#endif
}

}  // namespace null
