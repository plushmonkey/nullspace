#include "Game.h"

#include <cstdio>

#include "Memory.h"

namespace null {

#define SIM_TEST 1

void Simulate(Connection& connection, float dt);

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      connection(perm_arena, temp_arena),
      camera((float)width, (float)(height)) {}

bool Game::Initialize() {
  if (!tile_renderer.Initialize()) {
    return false;
  }

  if (!sprite_renderer.Initialize()) {
    return false;
  }

  return true;
}

void Game::Update(float dt) {
  static const Vector2f kBasePosition(512.0f, 512.f);
  static float timer = 0.0f;

  timer += dt * 0.0375f;

#if SIM_TEST
  Simulate(connection, dt);

  Player* me = connection.GetPlayerById(connection.player_id);
  if (me) {
    camera.position = me->position;
  }
#else
  camera.position = kBasePosition + Vector2f(std::cosf(timer) * 350.0f, std::sinf(timer) * 350.0f);
#endif

  if (tile_renderer.tilemap_texture == -1 && connection.login_state == Connection::LoginState::Complete) {
    tile_renderer.CreateMapBuffer(temp_arena, connection.map_handler.filename);
  }
}

void Game::Render() {
  tile_renderer.Render(camera);
  sprite_renderer.Render(camera);
}

// Test fun code
void Simulate(Connection& connection, float dt) {
  if (connection.login_state != Connection::LoginState::Complete) return;

  Player* player = connection.GetPlayerById(connection.player_id);
  if (!player) return;

  if (player->ship != 0) {
    player->ship = 0;
    player->position = Vector2f(512, 512);

#pragma pack(push, 1)
    struct {
      u8 type;
      u8 ship;
    } request = {0x18, 0};
#pragma pack(pop)

    printf("Sending ship request packet\n");

    connection.packet_sequencer.SendReliableMessage(connection, (u8*)&request, sizeof(request));
    return;
  }

  // static Vector2f waypoints[] = {Vector2f(570, 465), Vector2f(420, 450), Vector2f(480, 585), Vector2f(585, 545)};
  static Vector2f waypoints[] = {Vector2f(570, 455), Vector2f(455, 455), Vector2f(455, 570), Vector2f(570, 570)};
  static size_t waypoint_index = 0;

  Vector2f target = waypoints[waypoint_index];

  player->velocity = Normalize(target - player->position) * 12.0f;
  player->position += player->velocity * dt;
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
}

}  // namespace null
