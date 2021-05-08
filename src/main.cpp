#define NOMINMAX

#include <cstdio>

#include "Buffer.h"
#include "Memory.h"
#include "Tick.h"
#include "net/Checksum.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <memory.h>
#endif

#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/imgui.h>

#include "Game.h"
//
#include <GLFW/glfw3.h>

#ifndef _WIN32
extern GLFWwindow* clipboard_window;
#endif

// TODO: remove
#include <chrono>

#define OPENGL_DEBUG 0

// Specific opengl 4.x debug features that should never be in release
#if defined(_WIN32) && OPENGL_DEBUG

#define GL_DEBUG_TYPE_ERROR 0x824C
#define GL_DEBUG_OUTPUT 0x92E0

typedef void(APIENTRYP PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback, const void* userParam);

static PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;

void GLAPIENTRY OnOpenGLError(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                              const GLchar* message, const void* userParam);
#endif

namespace null {

enum class WindowType { Windowed, Fullscreen, BorderlessFullscreen };

constexpr bool kVerticalSync = false;
constexpr WindowType kWindowType = WindowType::Windowed;

const char* kPlayerName = "null space";
const char* kPlayerPassword = "none";

struct ServerInfo {
  const char* name;
  const char* server;
  u16 port;
};

ServerInfo kServers[] = {
    {"local", "127.0.0.1", 5000},
    {"subgame", "192.168.0.169", 5001},
    {"SSCE Hyperspace", "162.248.95.143", 5005},
    {"SSCJ Devastation", "69.164.220.203", 7022},
    {"SSCJ MetalGear CTF", "69.164.220.203", 14000},
};

constexpr size_t kServerIndex = 0;

static_assert(kServerIndex < NULLSPACE_ARRAY_SIZE(kServers), "Bad server index");

const char* kServerName = kServers[kServerIndex].name;
const char* kServerIp = kServers[kServerIndex].server;
u16 kServerPort = kServers[kServerIndex].port;

static void OnKeyboardChange(GLFWwindow* window, int key, int scancode, int key_action, int mods);
static void OnCharacter(GLFWwindow* window, unsigned int codepoint);

enum class GameScreen { MainMenu, Playing };

struct WindowState {
  InputState input;
  GameScreen screen = GameScreen::MainMenu;
};

struct nullspace {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  Game* game = nullptr;
  GLFWwindow* window = nullptr;
  WindowState window_state;

  int surface_width = 1366;
  int surface_height = 768;
  char name[20] = {0};
  char password[256] = {0};

  size_t selected_zone_index = 0;

  nullspace() : perm_arena(nullptr, 0), trans_arena(nullptr, 0) {}

  bool Initialize() {
    constexpr size_t kPermanentSize = Megabytes(64);
    constexpr size_t kTransientSize = Megabytes(32);

#ifdef _WIN32
    u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    u8* perm_memory = (u8*)malloc(kPermanentSize);
    u8* trans_memory = (u8*)malloc(kTransientSize);
#endif

    if (!perm_memory || !trans_memory) {
      fprintf(stderr, "Failed to allocate memory.\n");
      return false;
    }

    perm_arena = MemoryArena(perm_memory, kPermanentSize);
    trans_arena = MemoryArena(trans_memory, kTransientSize);

    window = CreateGameWindow(surface_width, surface_height);

    if (!window) {
      fprintf(stderr, "Failed to create window.\n");
      return false;
    }

    strcpy(name, kPlayerName);
    strcpy(password, kPlayerPassword);

    return true;
  }

  bool JoinZone(size_t selected_index) {
    kServerName = kServers[selected_index].name;
    kServerIp = kServers[selected_index].server;
    kServerPort = kServers[selected_index].port;

    perm_arena.Reset();

    kPlayerName = name;
    kPlayerPassword = password;

    if (!MemoryChecksumGenerator::Initialize(perm_arena, "cont_mem_text", "cont_mem_data")) {
      // TODO: Error pop up
      return false;
    }

    game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, surface_width, surface_height);

    if (!game->Initialize(window_state.input)) {
      // TODO: Error pop up
      fprintf(stderr, "Failed to create game\n");
      return false;
    }

    null::ConnectResult result = game->connection.Connect(kServerIp, kServerPort);

    if (result != null::ConnectResult::Success) {
      // TODO: Error pop up
      fprintf(stderr, "Failed to connect. Error: %d\n", (int)result);
      return false;
    }

    window_state.screen = GameScreen::Playing;

    game->connection.SendEncryptionRequest();
    return true;
  }

  bool HandleMainMenu() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) {
      JoinZone(selected_zone_index);
    }

    if (ImGui::Begin("Debug")) {
      ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      ImGui::End();
    }

    if (ImGui::Begin("Profile", 0, ImGuiWindowFlags_NoCollapse)) {
      ImGui::Text("Name");
      ImGui::PushItemWidth(-1);
      ImGui::InputText("##ProfileName", name, NULLSPACE_ARRAY_SIZE(name));
      ImGui::PopItemWidth();
      ImGui::Text("");
      ImGui::Text("Password");
      ImGui::PushItemWidth(-1);
      ImGui::InputText("##ProfilePassword", password, NULLSPACE_ARRAY_SIZE(password), ImGuiInputTextFlags_Password);
      ImGui::PopItemWidth();
      ImGui::End();
    }

    if (ImGui::Begin("Zones", 0, ImGuiWindowFlags_NoCollapse)) {
      ImGui::Text("Players Ping Zone Name");
      ImGui::Separator();

      if (ImGui::BeginListBox("##ZoneList", ImVec2(-FLT_MIN, 0.0f))) {
        for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kServers); ++i) {
          bool selected = selected_zone_index == i;
          char output[1024];

          // TODO: Get ping and player count
          u32 ping = 0;
          u32 player_count = 0;

          sprintf(output, "%6d %5d %s", ping, player_count, kServers[i].name);
          if (ImGui::Selectable(output, &selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selected_zone_index = i;

            if (ImGui::IsMouseDoubleClicked(0)) {
              JoinZone(selected_zone_index);
            }
          }
        }

        ImGui::EndListBox();
      }

      float width = ImGui::GetWindowWidth();
      float button_width = 60.0f;

      float center = width / 2.0f;

      ImGui::Separator();
      ImGui::Text("");
      ImGui::SetCursorPosX(center - button_width - 5.0f);
      if (ImGui::Button("Play", ImVec2(button_width, 0.0f))) {
        JoinZone(selected_zone_index);
      }

      ImGui::SameLine();
      ImGui::SetCursorPosX(center + 5.0f);

      if (ImGui::Button("Quit", ImVec2(button_width, 0.0f))) {
        return false;
      }
      ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Force mouse cursor back to arrow if enter was pressed while mouse was over text input.
    if (window_state.screen == GameScreen::Playing) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
      ImGui_ImplGlfw_NewFrame();
    }

    return true;
  }

  void Run() {
    // TODO: better timer
    using ms_float = std::chrono::duration<float, std::milli>;
    float frame_time = 0.0f;

    while (true) {
      auto start = std::chrono::high_resolution_clock::now();

      float dt = frame_time / 1000.0f;

      glfwPollEvents();

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      if (window_state.screen == GameScreen::MainMenu) {
        if (!HandleMainMenu()) {
          break;
        }
      } else {
        game->connection.Tick();
        if (!game->Update(window_state.input, dt)) {
          // Switch out of any active shaders so they can be cleaned up.
          glUseProgram(0);
          window_state.screen = GameScreen::MainMenu;
          game->Cleanup();
          continue;
        }
        game->Render(dt);
      }

      glfwSwapBuffers(window);

      if (glfwWindowShouldClose(window)) {
        break;
      }

      auto end = std::chrono::high_resolution_clock::now();
      frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

      trans_arena.Reset();
    }

    glfwTerminate();

    if (game && game->connection.connected) {
      game->connection.SendDisconnect();
      printf("Disconnected from server.\n");
    }
  }

  GLFWwindow* CreateGameWindow(int& width, int& height) {
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

    // TODO: monitor selection
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (kWindowType == WindowType::Fullscreen) {
      glfwWindowHint(GLFW_RED_BITS, mode->redBits);
      glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
      glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
      glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
      glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

      width = mode->width;
      height = mode->height;

      window = glfwCreateWindow(width, height, "nullspace", monitor, NULL);
    } else if (kWindowType == WindowType::BorderlessFullscreen) {
      glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

      width = mode->width;
      height = mode->height;

      window = glfwCreateWindow(width, height, "nullspace", NULL, NULL);
    } else {
      window = glfwCreateWindow(width, height, "nullspace", NULL, NULL);
    }

    if (!window) {
      glfwTerminate();
      return nullptr;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
      fprintf(stderr, "Failed to initialize opengl context");
      return nullptr;
    }

#if OPENGL_DEBUG
    HMODULE module = LoadLibrary("opengl32.dll");

    if (module) {
      typedef PROC (*GET_PROC_ADDR)(LPCSTR unnamedParam1);

      GET_PROC_ADDR get_addr = (GET_PROC_ADDR)GetProcAddress(module, "wglGetProcAddress");

      if (get_addr) {
        glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)get_addr("glDebugMessageCallback");

        if (glDebugMessageCallback) {
          glEnable(GL_DEBUG_OUTPUT);
          glDebugMessageCallback(OnOpenGLError, 0);
        }
      }
    }
#endif

    glViewport(0, 0, width, height);

    // Don't enable vsync with borderless fullscreen because glfw does dwm flushes instead.
    // There seems to be a bug with glfw that causes screen tearing if this is set.
    if (!(kVerticalSync && kWindowType == WindowType::BorderlessFullscreen)) {
      glfwSwapInterval(kVerticalSync);
    }

    glfwSetWindowUserPointer(window, &window_state);
    glfwSetKeyCallback(window, OnKeyboardChange);
    glfwSetCharCallback(window, OnCharacter);

#ifndef _WIN32
    clipboard_window = window;
#endif

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return window;
  }
};

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
    ActionKey(InputAction::Mine, GLFW_KEY_TAB, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Bomb, GLFW_KEY_TAB),
    ActionKey(InputAction::Bullet, GLFW_KEY_LEFT_CONTROL),
    ActionKey(InputAction::Bullet, GLFW_KEY_RIGHT_CONTROL),
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

static void OnKeyboardChange(GLFWwindow* window, int key, int scancode, int key_action, int mods) {
  WindowState* window_state = (WindowState*)glfwGetWindowUserPointer(window);

  // Specifically handle certain keys for os repeat control
  if (key == GLFW_KEY_BACKSPACE && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_BACKSPACE, mods);
  } else if (key == GLFW_KEY_ENTER && key_action == GLFW_PRESS) {
    window_state->input.OnCharacter(NULLSPACE_KEY_ENTER, mods);
  } else if (key == GLFW_KEY_ESCAPE && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_ESCAPE, mods);
  } else if (key == GLFW_KEY_V && key_action != GLFW_RELEASE) {
    if (mods & GLFW_MOD_CONTROL) {
      window_state->input.OnCharacter(NULLSPACE_KEY_PASTE, mods);
    }
  } else if (key == GLFW_KEY_PAGE_DOWN && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_PAGE_DOWN, mods);
  } else if (key == GLFW_KEY_PAGE_UP && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_PAGE_UP, mods);
  } else if (key == GLFW_KEY_LEFT_CONTROL && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_CONTROL, mods);
  } else if (key == GLFW_KEY_RIGHT_CONTROL && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_CONTROL, mods);
  } else if (key == GLFW_KEY_F2 && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_F2, mods);
  } else if (key == GLFW_KEY_END && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_END, mods);
  } else if (key == GLFW_KEY_DELETE && key_action != GLFW_RELEASE) {
    window_state->input.OnCharacter(NULLSPACE_KEY_DELETE, mods);
  }

  bool shift = key == GLFW_KEY_RIGHT_SHIFT || key == GLFW_KEY_LEFT_SHIFT;
  bool ctrl = key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL;
  // TODO: Redo all of the input. This is really bad
  if (shift || ctrl) {
    int mod = shift ? GLFW_MOD_SHIFT : GLFW_MOD_CONTROL;

    for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kDefaultKeys); ++i) {
      const ActionKey* action = kDefaultKeys + i;
      int req_mods = kDefaultKeys[i].mods;

      if ((req_mods & mod) && key_action == GLFW_RELEASE) {
        window_state->input.SetAction(action->action, false);
      }
    }
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

void OnCharacter(GLFWwindow* window, unsigned int codepoint) {
  if (codepoint < 256) {
    WindowState* window_state = (WindowState*)glfwGetWindowUserPointer(window);

    window_state->input.OnCharacter((char)codepoint);
  }
}
}  // namespace null

#if OPENGL_DEBUG
void GLAPIENTRY OnOpenGLError(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                              const GLchar* message, const void* userParam) {
  if (type == GL_DEBUG_TYPE_ERROR) {
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
  }
}
#endif

int main(void) {
  null::nullspace nullspace;

  if (!nullspace.Initialize()) {
    return 1;
  }

  nullspace.Run();

  return 0;
}
