#ifndef NULLSPACE_SETTINGS_H_
#define NULLSPACE_SETTINGS_H_

namespace null {

enum class EncryptMethod { Subspace, Continuum };
enum class WindowType { Windowed, Fullscreen, BorderlessFullscreen };

struct GameSettings {
  bool vsync;
  WindowType window_type;

  EncryptMethod encrypt_method;

  bool sound_enabled;
  float sound_volume;

  bool notify_max_prizes;
};

extern GameSettings g_Settings;

}  // namespace null

#endif
