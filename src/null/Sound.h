#ifndef NULLSPACE_SOUND_H_
#define NULLSPACE_SOUND_H_

#include <null/Memory.h>
//
#include <miniaudio.h>

#include <mutex>

namespace null {

enum {
  AudioClipFlag_Loaded = (1 << 0),
};
using AudioClipFlags = u32;

struct AudioClip {
  void* data;
  size_t size;

  AudioClipFlags flags;

  inline bool IsLoaded() const { return flags & AudioClipFlag_Loaded; }
};

enum class AudioType {
  None,

  Gun1,
  Gun2,
  Gun3,
  Gun4,

  Bomb1,
  Bomb2,
  Bomb3,
  Bomb4,
  EBomb1,
  EBomb2,
  EBomb3,
  EBomb4,

  Mine1,
  Mine2,
  Mine3,
  Mine4,

  Repel,
  Decoy,
  Burst,
  Thor,

  MultifireOn,
  MultifireOff,
  Stealth,
  Cloak,
  XRadar,
  Antiwarp,
  ToggleOff,

  Prize,

  Explode0,
  Explode1,
  Explode2,
  EBombExplode,

  Flag,
  Bounce,

  Portal,
  Warp,

  Count
};
constexpr size_t kAudioTypeCount = (size_t)AudioType::Count;

struct SoundSystem;

struct SoundDatabase {
  MemoryArena& perm_arena;

  AudioClip clips[kAudioTypeCount];

  AudioClip GetClip(AudioType type);
  AudioClip LoadClip(const char* filename);

 private:
  SoundDatabase(MemoryArena& perm_arena);
  friend struct SoundSystem;
};

struct PlayingAudioClip {
  ma_decoder decoder;
  float volume;

  PlayingAudioClip* next;
};

struct SoundSystem {
  bool initialized;

  ma_device_config cfg_device;
  ma_decoder_config cfg_decoder;

  ma_device device;

  SoundDatabase database;

  std::mutex clip_mutex;
  size_t playing_count = 0;
  PlayingAudioClip* playing_clips[64];
  PlayingAudioClip* free_clips = nullptr;

  SoundSystem(MemoryArena& perm_arena);

  bool Initialize();
  void Cleanup();

  void Play(AudioType type, float volume = 1.0f);

 private:
  void PlayClip(const AudioClip& clip, float volume);
};

}  // namespace null

#endif
