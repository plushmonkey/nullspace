#define NOMINMAX

#include <cstdio>

#include "Buffer.h"
#include "Memory.h"
#include "Tick.h"
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
  const char* name;
  const char* server;
  u16 port;
};

ServerInfo kServers[] = {
    {"local", "127.0.0.1", 5000},
    {"subgame", "192.168.0.13", 5001},
    {"SSCE Hyperspace", "162.248.95.143", 5005},
    {"SSCJ Devastation", "69.164.220.203", 7022},
};

constexpr size_t kServerIndex = 0;

static_assert(kServerIndex < NULLSPACE_ARRAY_SIZE(kServers), "Bad server index");

const char* kServerName = kServers[kServerIndex].name;
const char* kServerIp = kServers[kServerIndex].server;
const u16 kServerPort = kServers[kServerIndex].port;

struct ActionKey {
  int key;
  InputAction action;
  int mods;

  ActionKey(InputAction action, int key, int mods = 0) : key(key), action(action), mods(mods) {}
};

const ActionKey kDefaultKeys[] = {
    ActionKey(InputAction::Left, GLFW_KEY_LEFT),
    ActionKey(InputAction::Right, GLFW_KEY_RIGHT),
    ActionKey(InputAction::Forward, GLFW_KEY_UP),
    ActionKey(InputAction::Backward, GLFW_KEY_DOWN),
    ActionKey(InputAction::Afterburner, GLFW_KEY_LEFT_SHIFT),
    ActionKey(InputAction::Afterburner, GLFW_KEY_RIGHT_SHIFT),
    ActionKey(InputAction::Bomb, GLFW_KEY_TAB),
    ActionKey(InputAction::Bullet, GLFW_KEY_LEFT_CONTROL),
    ActionKey(InputAction::Bullet, GLFW_KEY_RIGHT_CONTROL),
    ActionKey(InputAction::Mine, GLFW_KEY_TAB, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Thor, GLFW_KEY_F6),
    ActionKey(InputAction::Burst, GLFW_KEY_DELETE, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Multifire, GLFW_KEY_DELETE),
    ActionKey(InputAction::Antiwarp, GLFW_KEY_END, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Stealth, GLFW_KEY_HOME),
    ActionKey(InputAction::Cloak, GLFW_KEY_HOME, GLFW_MOD_SHIFT),
    ActionKey(InputAction::XRadar, GLFW_KEY_END),
    ActionKey(InputAction::Repel, GLFW_KEY_LEFT_CONTROL, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Repel, GLFW_KEY_RIGHT_CONTROL, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Warp, GLFW_KEY_INSERT),
    ActionKey(InputAction::Portal, GLFW_KEY_INSERT, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Decoy, GLFW_KEY_F5),
    ActionKey(InputAction::Rocket, GLFW_KEY_F3),
    ActionKey(InputAction::Brick, GLFW_KEY_F4),
    ActionKey(InputAction::Attach, GLFW_KEY_F7),
    ActionKey(InputAction::PlayerListCycle, GLFW_KEY_F2),
    ActionKey(InputAction::PlayerListPrevious, GLFW_KEY_PAGE_UP),
    ActionKey(InputAction::PlayerListNext, GLFW_KEY_PAGE_DOWN),
    ActionKey(InputAction::PlayerListPreviousPage, GLFW_KEY_PAGE_UP, GLFW_MOD_SHIFT),
    ActionKey(InputAction::PlayerListNextPage, GLFW_KEY_PAGE_DOWN, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Play, GLFW_KEY_F11),
    ActionKey(InputAction::DisplayMap, GLFW_KEY_LEFT_ALT),
    ActionKey(InputAction::DisplayMap, GLFW_KEY_RIGHT_ALT),
    ActionKey(InputAction::ChatDisplay, GLFW_KEY_ESCAPE),
};

struct WindowState {
  InputState input;
};

void OnCharacter(GLFWwindow* window, unsigned int codepoint) {
  if (codepoint < 256) {
    WindowState* window_state = (WindowState*)glfwGetWindowUserPointer(window);

    window_state->input.OnCharacter((char)codepoint);
  }
}

void OnKeyboardChange(GLFWwindow* window, int key, int scancode, int key_action, int mods) {
  WindowState* window_state = (WindowState*)glfwGetWindowUserPointer(window);

  // Specifically handle certain keys for os repeat control
  if (key == GLFW_KEY_BACKSPACE && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_BACKSPACE, mods);
  } else if (key == GLFW_KEY_ENTER && key_action == GLFW_PRESS) {
    window_state->input.OnCharacter(NULLSPACE_KEY_ENTER, mods);
  } else if (key == GLFW_KEY_ESCAPE && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_ESCAPE, mods);
  } else if (key == GLFW_KEY_V && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter('v', mods);
  } else if (key == GLFW_KEY_PAGE_DOWN && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_PAGE_DOWN, mods);
  } else if (key == GLFW_KEY_PAGE_UP && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_PAGE_UP, mods);
  } else if (key == GLFW_KEY_LEFT_CONTROL && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_CONTROL, mods);
  } else if (key == GLFW_KEY_RIGHT_CONTROL && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_CONTROL, mods);
  }

  const ActionKey* action = nullptr;
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kDefaultKeys); ++i) {
    int req_mods = kDefaultKeys[i].mods;
    if (kDefaultKeys[i].key == key && (req_mods & mods) == req_mods) {
      action = kDefaultKeys + i;
      break;
    }
  }

  if (!action) return;

  if (key_action == GLFW_PRESS) {
    window_state->input.SetAction(action->action, true);
  } else if (key_action == GLFW_RELEASE) {
    window_state->input.SetAction(action->action, false);
  }
}

struct nullspace {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  Game* game = nullptr;
  GLFWwindow* window = nullptr;
  WindowState window_state;

  nullspace() : perm_arena(nullptr, 0), trans_arena(nullptr, 0) {}

  bool Initialize() {
    constexpr int kWidth = 1366;
    constexpr int kHeight = 768;

    constexpr size_t kPermanentSize = Megabytes(64);
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

    if (!game->Initialize(window_state.input)) {
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

    u32 last_tick = GetCurrentTick();

    while (connection->connected) {
      auto start = std::chrono::high_resolution_clock::now();
      u32 tick = GetCurrentTick();

      float dt = frame_time / 1000.0f;
      connection->Tick();

      glfwPollEvents();

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      s32 ticks = TICK_DIFF(tick, last_tick);
      for (s32 i = 0; i < ticks; ++i) {
        game->Update(window_state.input, 1.0f / 100.0f);
        last_tick = tick;
      }

      game->Render(dt);

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
    glfwSetWindowUserPointer(window, &window_state);
    glfwSetKeyCallback(window, OnKeyboardChange);
    glfwSetCharCallback(window, OnCharacter);

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
