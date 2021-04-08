#define NOMINMAX

#include <cstdio>

#include "Buffer.h"
#include "Memory.h"
#include "net/Checksum.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include "Game.h"
//
#include <GLFW/glfw3.h>

// TODO: remove
#include <chrono>

namespace null {

constexpr bool kVerticalSync = false;

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

struct nullspace {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  Game* game = nullptr;
  GLFWwindow* window = nullptr;

  nullspace() : perm_arena(nullptr, 0), trans_arena(nullptr, 0) {}

  bool Initialize() {
    constexpr int kWidth = 1360;
    constexpr int kHeight = 768;

    constexpr size_t kPermanentSize = Megabytes(32);
    constexpr size_t kTransientSize = Megabytes(32);

    u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!perm_memory || !trans_memory) {
      fprintf(stderr, "Failed to allocate memory.\n");
      return false;
    }

    perm_arena = MemoryArena(perm_memory, kPermanentSize);
    trans_arena = MemoryArena(trans_memory, kTransientSize);

    if (!MemoryChecksumGenerator::Initialize(perm_arena, "cont_mem_text", "cont_mem_data")) {
      return false;
    }

    window = CreateGameWindow(kWidth, kHeight);

    if (!window) {
      fprintf(stderr, "Failed to create window.\n");
      return false;
    }

    game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, kWidth, kHeight);

    if (!game->Initialize()) {
      fprintf(stderr, "Failed to create game\n");
      return false;
    }

    return true;
  }

  void Run() {
    Connection* connection = &game->connection;
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

    while (connection->connected) {
      auto start = std::chrono::high_resolution_clock::now();

      float dt = frame_time / 1000.0f;
      connection->Tick();

      glfwPollEvents();

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      game->Update(dt);
      game->Render();

      glfwSwapBuffers(window);

      if (glfwWindowShouldClose(window)) {
        glfwTerminate();
        break;
      }

      auto end = std::chrono::high_resolution_clock::now();
      frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

      trans_arena.Reset();
    }

    buffer.Reset();
    buffer.WriteU8(0x00);  // Core
    buffer.WriteU8(0x07);  // Disconnect

    connection->Send(buffer);
    printf("Disconnected from server.\n");
  }

  GLFWwindow* CreateGameWindow(int width, int height) {
    GLFWwindow* window = nullptr;

    if (!glfwInit()) {
      fprintf(stderr, "Failed to initialize window system.\n");
      return nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_SAMPLES, 0);

    window = glfwCreateWindow(width, height, "nullspace", NULL, NULL);
    if (!window) {
      glfwTerminate();
      return nullptr;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
      fprintf(stderr, "Failed to initialize opengl context");
      return nullptr;
    }

    glViewport(0, 0, width, height);

    glfwSwapInterval(kVerticalSync);

    return window;
  }
};

}  // namespace null

int main(void) {
  null::nullspace nullspace;

  if (!nullspace.Initialize()) {
    return 1;
  }

  nullspace.Run();

  return 0;
}
