#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>

#include <src/Memory.h>
#include <src/Game.h>
#include <src/Platform.h>
#include <src/net/Connection.h>
#include <src/render/Image.h>
#include <chrono>

// Data
static EGLDisplay           g_EglDisplay = EGL_NO_DISPLAY;
static EGLSurface           g_EglSurface = EGL_NO_SURFACE;
static EGLContext           g_EglContext = EGL_NO_CONTEXT;
static struct android_app*  g_App = NULL;
static bool                 g_Initialized = false;
static char                 g_LogTag[] = "nullspace";

// Forward declarations of helper functions
static int ShowSoftKeyboardInput();
static int PollUnicodeChars();
static unsigned char* LoadImage(const char* filename, size_t* size);
static void AndroidErrorLogger(const char* fmt, ...);
static const char* AndroidGetStoragePath(null::MemoryArena& arena, const char* path);
void shutdown();

namespace null {

constexpr EncryptMethod kEncryptMethod = EncryptMethod::Subspace;

const char* kPlayerName = "null space";
const char* kPlayerPassword = "none";

struct ServerInfo {
    const char* name;
    const char* server;
    u16 port;
};

ServerInfo kServers[] = {
        {"local", "10.0.2.2", 5000},
        {"subgame", "10.0.2.2", 5002},
        {"SSCE Hyperspace", "162.248.95.143", 5005},
        {"SSCJ Devastation", "69.164.220.203", 7022},
        {"SSCJ MetalGear CTF", "69.164.220.203", 14000},
};

constexpr size_t kServerIndex = 0;

static_assert(kServerIndex < NULLSPACE_ARRAY_SIZE(kServers), "Bad server index");

const char* kServerName = kServers[kServerIndex].name;
const char* kServerIp = kServers[kServerIndex].server;
u16 kServerPort = kServers[kServerIndex].port;

InputState g_InputState;
enum class GameScreen { MainMenu, Playing };

MemoryArena* perm_global = nullptr;

// TODO: Merge this with existing main code
struct nullspace {
    MemoryArena perm_arena;
    MemoryArena trans_arena;
    Game* game = nullptr;
    int surface_width = 0;
    int surface_height = 0;
    char name[20];
    char password[20];
    GameScreen screen = GameScreen::MainMenu;
    float frame_time = 0.0f;

    size_t selected_zone_index = 0;

    bool Initialize() {
        constexpr size_t kPermanentSize = Megabytes(64);
        constexpr size_t kTransientSize = Megabytes(32);

        u8* perm_memory = (u8*)malloc(kPermanentSize);
        u8* trans_memory = (u8*)malloc(kTransientSize);

        if (!perm_memory || !trans_memory) {
            fprintf(stderr, "Failed to allocate memory.\n");
            return false;
        }

        perm_arena = MemoryArena(perm_memory, kPermanentSize);
        trans_arena = MemoryArena(trans_memory, kTransientSize);

        perm_global = &perm_arena;

        strcpy(name, kPlayerName);
        strcpy(password, kPlayerPassword);

        null::image_loader = LoadImage;
        null::log_error = AndroidErrorLogger;
        null::GetStoragePath = AndroidGetStoragePath;

        return true;
    }

    bool JoinZone(size_t selected_index) {
        kServerName = kServers[selected_index].name;
        kServerIp = kServers[selected_index].server;
        kServerPort = kServers[selected_index].port;

        perm_arena.Reset();

        kPlayerName = name;
        kPlayerPassword = password;

        game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, surface_width, surface_height);

        if (!game->Initialize(g_InputState)) {
            // TODO: Error pop up
            __android_log_print(ANDROID_LOG_DEBUG, g_LogTag, "Failed to create game.");
            return false;
        }

        null::ConnectResult result = game->connection.Connect(kServerIp, kServerPort);

        if (result != null::ConnectResult::Success) {
            // TODO: Error pop up
            __android_log_print(ANDROID_LOG_DEBUG, g_LogTag, "Failed to connect. Error: %d.", (int)result);
            return false;
        }

        screen = GameScreen::Playing;

        game->connection.SendEncryptionRequest(EncryptMethod::Subspace);

        return true;
    }

    bool HandleMainMenu() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
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
        if (screen == GameScreen::Playing) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            ImGui_ImplAndroid_NewFrame();
        }

        return true;
    }

    bool Update() {
        ImGuiIO& io = ImGui::GetIO();
        surface_width = (null::u32)io.DisplaySize.x;
        surface_height = (null::u32)io.DisplaySize.y;
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);

        using ms_float = std::chrono::duration<float, std::milli>;
        auto start = std::chrono::high_resolution_clock::now();

        float dt = frame_time / 1000.0f;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (screen == GameScreen::MainMenu) {
            if (!HandleMainMenu()) {
                return false;
            }
        } else {
            game->connection.Tick();

            if (!game->Update(g_InputState, dt)) {
                // Switch out of any active shaders so they can be cleaned up.
                glUseProgram(0);
                screen = GameScreen::MainMenu;
                game->Cleanup();
                return true;
            }

            game->Render(dt);
        }

        eglSwapBuffers(g_EglDisplay, g_EglSurface);

        auto end = std::chrono::high_resolution_clock::now();
        frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

        trans_arena.Reset();
        return true;
    }
};

static nullspace g_nullspace;

}  // namespace null

void init(struct android_app* app)
{
    if (g_Initialized)
        return;

    __android_log_print(ANDROID_LOG_INFO, g_LogTag, "%s", "Initializing app.");

    void* output = nullptr;

    g_App = app;
    ANativeWindow_acquire(g_App->window);

    // Initialize EGL
    // This is mostly boilerplate code for EGL...
    {
        g_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (g_EglDisplay == EGL_NO_DISPLAY)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglGetDisplay(EGL_DEFAULT_DISPLAY) returned EGL_NO_DISPLAY");

        if (eglInitialize(g_EglDisplay, 0, 0) != EGL_TRUE)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglInitialize() returned with an error");

        const EGLint egl_attributes[] = { EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
        EGLint num_configs = 0;
        if (eglChooseConfig(g_EglDisplay, egl_attributes, nullptr, 0, &num_configs) != EGL_TRUE)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglChooseConfig() returned with an error");
        if (num_configs == 0)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglChooseConfig() returned 0 matching config");

        // Get the first matching config
        EGLConfig egl_config;
        eglChooseConfig(g_EglDisplay, egl_attributes, &egl_config, 1, &num_configs);
        EGLint egl_format;
        eglGetConfigAttrib(g_EglDisplay, egl_config, EGL_NATIVE_VISUAL_ID, &egl_format);
        ANativeWindow_setBuffersGeometry(g_App->window, 0, 0, egl_format);

        const EGLint egl_context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        g_EglContext = eglCreateContext(g_EglDisplay, egl_config, EGL_NO_CONTEXT, egl_context_attributes);

        if (g_EglContext == EGL_NO_CONTEXT)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglCreateContext() returned EGL_NO_CONTEXT");

        g_EglSurface = eglCreateWindowSurface(g_EglDisplay, egl_config, g_App->window, NULL);
        eglMakeCurrent(g_EglDisplay, g_EglSurface, g_EglSurface, g_EglContext);

        if (!gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress)) {
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "Failed to initialize glad loader.");
        }
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Disable loading/saving of .ini file from disk.
    // FIXME: Consider using LoadIniSettingsFromMemory() / SaveIniSettingsToMemory() to save in appropriate location for Android.
    io.IniFilename = NULL;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplAndroid_Init(g_App->window);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Load Fonts
    ImFontConfig font_cfg;
    font_cfg.SizePixels = 22.0f;
    io.Fonts->AddFontDefault(&font_cfg);

    // Arbitrary scale-up
    // FIXME: Put some effort into DPI awareness
    ImGui::GetStyle().ScaleAllSizes(3.0f);

    g_Initialized = true;

    null::g_nullspace.Initialize();
}

void tick()
{
    if (g_EglDisplay == EGL_NO_DISPLAY)
        return;

    if (!null::g_nullspace.Update()) {
        exit(0);
        return;
    }
}

void shutdown()
{
    if (!g_Initialized)
        return;

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();

    if (g_EglDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(g_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (g_EglContext != EGL_NO_CONTEXT)
            eglDestroyContext(g_EglDisplay, g_EglContext);

        if (g_EglSurface != EGL_NO_SURFACE)
            eglDestroySurface(g_EglDisplay, g_EglSurface);

        eglTerminate(g_EglDisplay);
    }

    g_EglDisplay = EGL_NO_DISPLAY;
    g_EglContext = EGL_NO_CONTEXT;
    g_EglSurface = EGL_NO_SURFACE;
    ANativeWindow_release(g_App->window);

    g_Initialized = false;
#if 0
    if (null::g_nullspace.game) {
        null::g_nullspace.game->connection.SendDisconnect();
    }
#endif
}

static void handleAppCmd(struct android_app* app, int32_t appCmd)
{
    switch (appCmd)
    {
        case APP_CMD_SAVE_STATE:
            break;
        case APP_CMD_INIT_WINDOW:
            init(app);
            break;
        case APP_CMD_TERM_WINDOW:
            shutdown();
            break;
        case APP_CMD_GAINED_FOCUS:
            break;
        case APP_CMD_LOST_FOCUS:
            break;
    }
}

static int32_t handleInputEvent(struct android_app* app, AInputEvent* inputEvent) {
    return ImGui_ImplAndroid_HandleInputEvent(inputEvent);
}

void android_main(struct android_app* app) {
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInputEvent;

    while (true)
    {
        int out_events;
        struct android_poll_source* out_data;

        // Poll all events. If the app is not visible, this loop blocks until g_Initialized == true.
        while (ALooper_pollAll(g_Initialized ? 0 : -1, NULL, &out_events, (void**)&out_data) >= 0)
        {
            // Process one event
            if (out_data != NULL)
                out_data->process(app, out_data);

            // Exit the app by returning from within the infinite loop
            if (app->destroyRequested != 0)
            {
                // shutdown() should have been called already while processing the
                // app command APP_CMD_TERM_WINDOW. But we play save here
                if (!g_Initialized)
                    shutdown();

                return;
            }
        }

        // Initiate a new frame
        tick();
    }
}

// Unfortunately, there is no way to show the on-screen input from native code.
// Therefore, we call ShowSoftKeyboardInput() of the main activity implemented in MainActivity.kt via JNI.
static int ShowSoftKeyboardInput()
{
    JavaVM* java_vm = g_App->activity->vm;
    JNIEnv* java_env = NULL;

    jint jni_return = java_vm->GetEnv((void**)&java_env, JNI_VERSION_1_6);
    if (jni_return == JNI_ERR)
        return -1;

    jni_return = java_vm->AttachCurrentThread(&java_env, NULL);
    if (jni_return != JNI_OK)
        return -2;

    jclass native_activity_clazz = java_env->GetObjectClass(g_App->activity->clazz);
    if (native_activity_clazz == NULL)
        return -3;

    jmethodID method_id = java_env->GetMethodID(native_activity_clazz, "showSoftInput", "()V");
    if (method_id == NULL)
        return -4;

    java_env->CallVoidMethod(g_App->activity->clazz, method_id);

    jni_return = java_vm->DetachCurrentThread();
    if (jni_return != JNI_OK)
        return -5;

    return 0;
}

static const char* AndroidGetStoragePath(null::MemoryArena& arena, const char* path) {
    JavaVM* java_vm = g_App->activity->vm;
    JNIEnv* env = NULL;

    jint jni_return = java_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (jni_return == JNI_ERR)
        return nullptr;

    jni_return = java_vm->AttachCurrentThread(&env, NULL);
    if (jni_return != JNI_OK)
        return nullptr;

    jclass cls_Env = env->FindClass("android/app/NativeActivity");
    jmethodID mid = env->GetMethodID(cls_Env, "getExternalFilesDir",
                                     "(Ljava/lang/String;)Ljava/io/File;");
    jobject obj_File = env->CallObjectMethod(g_App->activity->clazz, mid, NULL);
    jclass cls_File = env->FindClass("java/io/File");
    jmethodID mid_getPath = env->GetMethodID(cls_File, "getPath",
                                             "()Ljava/lang/String;");
    jstring external_path = (jstring) env->CallObjectMethod(obj_File, mid_getPath);

    const char* c_path = env->GetStringUTFChars(external_path, NULL);

    char* result = (char*)arena.Allocate(strlen(c_path) + strlen(path) + 1);

    sprintf(result, "%s/%s", c_path, path);

    env->ReleaseStringUTFChars(external_path, c_path);
    java_vm->DetachCurrentThread();

    return result;
}

// Unfortunately, the native KeyEvent implementation has no getUnicodeChar() function.
// Therefore, we implement the processing of KeyEvents in MainActivity.kt and poll
// the resulting Unicode characters here via JNI and send them to Dear ImGui.
static int PollUnicodeChars()
{
    JavaVM* java_vm = g_App->activity->vm;
    JNIEnv* java_env = NULL;

    jint jni_return = java_vm->GetEnv((void**)&java_env, JNI_VERSION_1_6);
    if (jni_return == JNI_ERR)
        return -1;

    jni_return = java_vm->AttachCurrentThread(&java_env, NULL);
    if (jni_return != JNI_OK)
        return -2;

    jclass native_activity_clazz = java_env->GetObjectClass(g_App->activity->clazz);
    if (native_activity_clazz == NULL)
        return -3;

    jmethodID method_id = java_env->GetMethodID(native_activity_clazz, "pollUnicodeChar", "()I");
    if (method_id == NULL)
        return -4;

    // Send the actual characters to Dear ImGui
    ImGuiIO& io = ImGui::GetIO();
    jint unicode_character;
    while ((unicode_character = java_env->CallIntMethod(g_App->activity->clazz, method_id)) != 0)
        io.AddInputCharacter(unicode_character);

    jni_return = java_vm->DetachCurrentThread();
    if (jni_return != JNI_OK)
        return -5;

    return 0;
}

static unsigned char* LoadImage(const char* filename, size_t* size) {
    unsigned char* data = nullptr;
    *size = 0;

    AAsset* asset_descriptor = AAssetManager_open(g_App->activity->assetManager, filename, AASSET_MODE_BUFFER);
    if (asset_descriptor) {
        *size = AAsset_getLength(asset_descriptor);
        data = (unsigned char*)malloc(*size);
        int64_t num_bytes_read = AAsset_read(asset_descriptor, data, *size);
        AAsset_close(asset_descriptor);
        IM_ASSERT(num_bytes_read == *size);
    }

    return data;
}

static void AndroidErrorLogger(const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, g_LogTag, fmt, args);
    va_end(args);
}
