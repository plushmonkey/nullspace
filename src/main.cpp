#define NOMINMAX

#include <cstdio>

#include "Buffer.h"
#include "Checksum.h"
#include "Memory.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include "net/Connection.h"

// TODO: remove
#include <chrono>

//#define SIM_TEST

namespace null {

const char* kPlayerName = "nullspace";
const char* kPlayerPassword = "none";

struct ServerInfo {
  const char* server;
  u16 port;
};

ServerInfo kServers[] = {
    {"127.0.0.1", 5000},       // Local
    {"192.168.0.13", 5001},    // Subgame
    {"162.248.95.143", 5005},  // Hyperspace
    {"69.164.220.203", 7022},  // Devastation
};

constexpr size_t kServerIndex = 0;

static_assert(kServerIndex < sizeof(kServers) / sizeof(*kServers), "Bad server index");

const char* kServerIp = kServers[kServerIndex].server;
const u16 kServerPort = kServers[kServerIndex].port;

char* LoadFile(MemoryArena& arena, const char* path) {
#pragma warning(push)
#pragma warning(disable : 4996)
  FILE* f = fopen(path, "rb");
#pragma warning(pop)

  if (!f) {
    return nullptr;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* data = (char*)arena.Allocate(size);

  fread(data, 1, size, f);
  fclose(f);

  return data;
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

    connection.packet_sequencer.SendReliableMessage(connection, (u8*)&request, sizeof(request));
    return;
  }

  static Vector2f waypoints[] = {Vector2f(570, 465), Vector2f(420, 450), Vector2f(480, 585), Vector2f(585, 545)};
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

void run() {
  constexpr size_t kPermanentSize = Megabytes(32);
  constexpr size_t kTransientSize = Megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  char* mem_text = LoadFile(perm_arena, "cont_mem_text");
  char* mem_data = LoadFile(perm_arena, "cont_mem_data");

  if (!mem_text || !mem_data) {
    fprintf(stderr, "Requires Continuum dumped memory files cont_mem_text and cont_mem_data\n");
    return;
  }

  MemoryChecksumGenerator::Initialize(mem_text, mem_data);

  Connection* connection = memory_arena_construct_type(&perm_arena, Connection, perm_arena, trans_arena);

  null::ConnectResult result = connection->Connect(kServerIp, kServerPort);

  if (result != null::ConnectResult::Success) {
    fprintf(stderr, "Failed to connect. Error: %d\n", (int)result);
    return;
  }

  // Send Continuum encryption request
  NetworkBuffer buffer(perm_arena, kMaxPacketSize);

  buffer.WriteU8(0x00);         // Core
  buffer.WriteU8(0x01);         // Encryption request
  buffer.WriteU32(0x00000000);  // Key
  buffer.WriteU16(0x11);        // Version

  connection->Send(buffer);

  // TODO: better timer
  using ms_float = std::chrono::duration<float, std::milli>;
  float frame_time = 0.0f;

  if (!connection->render.Initialize(1360, 768)) {
    fprintf(stderr, "Failed to initialize renderer.\n");
    return;
  }

  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    connection->Tick();

#ifdef SIM_TEST
    Simulate(*connection, frame_time / 1000.0f);
#endif

    if (!connection->render.Render(frame_time / 1000.0f)) {
      break;
    }

    auto end = std::chrono::high_resolution_clock::now();
    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    trans_arena.Reset();
  }
}

}  // namespace null

int main(void) {
  null::run();

  return 0;
}
