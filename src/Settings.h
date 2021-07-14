#ifndef NULLSPACE_SETTINGS_H_
#define NULLSPACE_SETTINGS_H_

#include "Types.h"

namespace null {

enum class EncryptMethod { Subspace, Continuum };
enum class WindowType { Windowed, Fullscreen, BorderlessFullscreen };

struct GameSettings {
  bool vsync;
  WindowType window_type;
  bool render_stars;

  EncryptMethod encrypt_method;

  bool sound_enabled;
  float sound_volume;
  // How many tiles outside of the screen that you can still hear sounds.
  float sound_radius_increase;

  bool notify_max_prizes;
  u16 target_bounty;

  u32 chat_namelen = 10;
};

extern GameSettings g_Settings;

}  // namespace null

#endif
