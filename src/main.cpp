#define NOMINMAX

#include <algorithm>
#include <cstdio>

#include "Buffer.h"
#include "Memory.h"
#include "Settings.h"
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

GameSettings g_Settings;

void InitializeSettings() {
  g_Settings.vsync = true;
  g_Settings.window_type = WindowType::Windowed;
  g_Settings.render_stars = true;

  g_Settings.encrypt_method = EncryptMethod::Continuum;

  g_Settings.sound_enabled = true;
  g_Settings.sound_volume = 0.15f;
  g_Settings.sound_radius_increase = 10.0f;

  g_Settings.notify_max_prizes = false;
  g_Settings.target_bounty = 20;
}

const char* kPlayerName = "null space";
const char* kPlayerPassword = "none";

struct ServerInfo {
  const char* name;
  const char* server;
  u16 port;
};

ServerInfo kServers[] = {
    {"local", "192.168.0.169", 5000},
    {"subgame", "192.168.0.169", 5002},
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

struct ActionKey {
  int key;
  InputAction action;
  int mods;

  ActionKey() {}
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
    ActionKey(InputAction::Repel, GLFW_KEY_GRAVE_ACCENT),
    ActionKey(InputAction::Warp, GLFW_KEY_INSERT),
    ActionKey(InputAction::Portal, GLFW_KEY_INSERT, GLFW_MOD_SHIFT),
    ActionKey(InputAction::Decoy, GLFW_KEY_F5),
    ActionKey(InputAction::Rocket, GLFW_KEY_F3),
    ActionKey(InputAction::Brick, GLFW_KEY_F4),
    ActionKey(InputAction::Attach, GLFW_KEY_F7),
    ActionKey(InputAction::StatBoxCycle, GLFW_KEY_F2),
    ActionKey(InputAction::StatBoxPrevious, GLFW_KEY_PAGE_UP),
    ActionKey(InputAction::StatBoxNext, GLFW_KEY_PAGE_DOWN),
    ActionKey(InputAction::StatBoxPreviousPage, GLFW_KEY_PAGE_UP, GLFW_MOD_SHIFT),
    ActionKey(InputAction::StatBoxNextPage, GLFW_KEY_PAGE_DOWN, GLFW_MOD_SHIFT),
    ActionKey(InputAction::StatBoxHelpNext, GLFW_KEY_F1),
    ActionKey(InputAction::Play, GLFW_KEY_F11),
    ActionKey(InputAction::DisplayMap, GLFW_KEY_LEFT_ALT),
    ActionKey(InputAction::DisplayMap, GLFW_KEY_RIGHT_ALT),
    ActionKey(InputAction::ChatDisplay, GLFW_KEY_ESCAPE),
};

struct WindowState {
  InputState input;
  GameScreen screen = GameScreen::MainMenu;
  Game* game = nullptr;
  ActionKey keys[NULLSPACE_ARRAY_SIZE(kDefaultKeys)];
};

MemoryArena* perm_global = nullptr;

struct nullspace {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  MemoryArena work_arena;
  WorkQueue* work_queue;
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
    constexpr size_t kWorkSize = Megabytes(4);

#ifdef _WIN32
    u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    u8* work_memory = (u8*)VirtualAlloc(NULL, kWorkSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    u8* perm_memory = (u8*)malloc(kPermanentSize);
    u8* trans_memory = (u8*)malloc(kTransientSize);
    u8* work_memory = (u8*)malloc(kWorkSize);
#endif

    if (!perm_memory || !trans_memory || !work_memory) {
      fprintf(stderr, "Failed to allocate memory.\n");
      return false;
    }

    perm_arena = MemoryArena(perm_memory, kPermanentSize);
    trans_arena = MemoryArena(trans_memory, kTransientSize);
    work_arena = MemoryArena(work_memory, kWorkSize);

    work_queue = new WorkQueue(work_arena);

    perm_global = &perm_arena;

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

    if (g_Settings.encrypt_method == EncryptMethod::Continuum &&
        !MemoryChecksumGenerator::Initialize(perm_arena, "cont_mem_text", "cont_mem_data")) {
      // TODO: Error pop up
      return false;
    }

    game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, *work_queue, surface_width,
                                       surface_height);

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

    memcpy(window_state.keys, kDefaultKeys, sizeof(kDefaultKeys));

    // Sort keys so the mods will be checked first
    std::sort(window_state.keys, window_state.keys + NULLSPACE_ARRAY_SIZE(kDefaultKeys),
              [](const ActionKey& l, const ActionKey& r) { return l.mods > r.mods; });

    window_state.screen = GameScreen::Playing;
    window_state.game = game;

    game->connection.SendEncryptionRequest(g_Settings.encrypt_method);
    return true;
  }

  bool HandleMainMenu() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) {
      JoinZone(selected_zone_index);
    }

    ImGui::SetNextWindowPos(ImVec2(surface_width - 212.0f, 2), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(210, 50), ImGuiCond_Always);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Debug", 0, window_flags)) {
      ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      ImGui::End();
    }

    float profile_width = 250;
    float profile_height = 140;

    ImGui::SetNextWindowPos(
        ImVec2(surface_width / 4.0f - profile_width / 2, surface_height / 2.0f - profile_height / 2), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(profile_width, profile_height), ImGuiCond_Always);

    if (ImGui::Begin("Profile", 0, window_flags)) {
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

    float zones_width = 300;
    float zones_height = 230;

    ImGui::SetNextWindowPos(ImVec2(surface_width / 2.0f + zones_width / 2, surface_height / 2.0f - zones_height / 2),
                            ImGuiCond_Always);

    ImGui::SetNextWindowSize(ImVec2(zones_width, zones_height), ImGuiCond_Always);

    if (ImGui::Begin("Zones", 0, window_flags)) {
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
    constexpr float kMaxDelta = 1.0f / 20.0f;

    // TODO: better timer
    using ms_float = std::chrono::duration<float, std::milli>;
    float frame_time = 0.0f;

    while (true) {
      auto start = std::chrono::high_resolution_clock::now();

      float dt = frame_time / 1000.0f;

      // Cap dt so window movement doesn't cause large updates
      if (dt > kMaxDelta) {
        dt = kMaxDelta;
      }

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
          window_state.game = nullptr;
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

    if (g_Settings.window_type == WindowType::Fullscreen) {
      glfwWindowHint(GLFW_RED_BITS, mode->redBits);
      glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
      glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
      glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
      glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

      width = mode->width;
      height = mode->height;

      window = glfwCreateWindow(width, height, "nullspace", monitor, NULL);
    } else if (g_Settings.window_type == WindowType::BorderlessFullscreen) {
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
    if (!(g_Settings.vsync && g_Settings.window_type == WindowType::BorderlessFullscreen)) {
      glfwSwapInterval(g_Settings.vsync);
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

  ActionKey* keys = window_state->keys;

  const ActionKey* action = nullptr;
  bool used_action = false;

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kDefaultKeys); ++i) {
    int req_mods = keys[i].mods;

    if (keys[i].key == key && (req_mods & mods) == req_mods) {
      action = keys + i;

      if (key_action != GLFW_RELEASE) {
        if (!used_action) {
          window_state->input.OnAction(action->action);
          used_action = true;
        }

        window_state->input.SetAction(action->action, true);
      } else if (key_action == GLFW_RELEASE) {
        window_state->input.SetAction(action->action, false);
      }
    }
  }
}

void OnCharacter(GLFWwindow* window, unsigned int codepoint) {
  if (codepoint < 256) {
    WindowState* window_state = (WindowState*)glfwGetWindowUserPointer(window);

    // Loop over the actions and don't send the keypress if the chat is currently empty
    if (window_state->game) {
      for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(kDefaultKeys); ++i) {
        if (kDefaultKeys[i].key == codepoint) {
          if (window_state->game->chat.input[0] == 0) {
            return;
          }
        }
      }
    }

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

  null::InitializeSettings();

  if (!nullspace.Initialize()) {
    return 1;
  }

  nullspace.Run();

  return 0;
}
