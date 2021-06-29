#define MINIAUDIO_IMPLEMENTATION
#include "Sound.h"

#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "Settings.h"

#define SOUND_SAMPLE_FORMAT ma_format_f32
#define SOUND_CHANNEL_COUNT 2
#define SOUND_SAMPLE_RATE 44100

namespace null {

ma_uint32 read_and_mix_pcm_frames_f32(ma_decoder* decoder, float* output, ma_uint32 frame_count) {
  float buffer[4096];
  ma_uint32 total_frames_read = 0;
  int iterations = 0;

  while (total_frames_read < frame_count) {
    ma_uint32 frames_remaining = frame_count - total_frames_read;
    ma_uint32 frame_request_count = ma_countof(buffer) / SOUND_CHANNEL_COUNT;

    if (frame_request_count > frames_remaining) {
      frame_request_count = frames_remaining;
    }

    ma_uint32 frames_read_count = (ma_uint32)ma_decoder_read_pcm_frames(decoder, buffer, frame_request_count);
    if (frames_read_count == 0) {
      break;
    }

    // Perform add mix
    for (ma_uint32 sample_index = 0; sample_index < frames_read_count * SOUND_CHANNEL_COUNT; ++sample_index) {
      output[total_frames_read * SOUND_CHANNEL_COUNT + sample_index] += buffer[sample_index] * g_Settings.sound_volume;
    }

    total_frames_read += frames_read_count;

    if (frames_read_count < frame_request_count) {
      // End of clip
      break;
    }
    ++iterations;
  }

  return total_frames_read;
}

void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
  SoundSystem* system = (SoundSystem*)device->pUserData;
  float* output_f32 = (float*)output;

  MA_ASSERT(device->playback.format == SOUND_SAMPLE_FORMAT);

  std::lock_guard<std::mutex> guard(system->clip_mutex);

  assert(system->playing_count < NULLSPACE_ARRAY_SIZE(system->playing_clips));

  for (size_t i = 0; i < system->playing_count; ++i) {
    PlayingAudioClip* clip = system->playing_clips[i];
    ma_decoder* decoder = &clip->decoder;

    ma_uint32 frames_read = read_and_mix_pcm_frames_f32(decoder, output_f32, frame_count);

    ma_uint64 available = 1;
    ma_decoder_get_available_frames(decoder, &available);

    // Check if clip ended
    if (available == 0 || frames_read < frame_count) {
      ma_decoder_uninit(decoder);

      // Remove clip from playing list and send it to free list
      system->playing_clips[i--] = system->playing_clips[--system->playing_count];
      clip->next = system->free_clips;
      system->free_clips = clip->next;
    }
  }

  (void)input;
}

SoundSystem::SoundSystem(MemoryArena& perm_arena) : database(perm_arena), initialized(false) {
  cfg_decoder = ma_decoder_config_init(SOUND_SAMPLE_FORMAT, SOUND_CHANNEL_COUNT, SOUND_SAMPLE_RATE);

  cfg_device = ma_device_config_init(ma_device_type_playback);

  cfg_device.playback.format = SOUND_SAMPLE_FORMAT;
  cfg_device.playback.channels = SOUND_CHANNEL_COUNT;
  cfg_device.sampleRate = SOUND_SAMPLE_RATE;
  cfg_device.dataCallback = data_callback;
  cfg_device.pUserData = this;
}

bool SoundSystem::Initialize() {
  if (ma_device_init(NULL, &cfg_device, &device) != MA_SUCCESS) {
    return false;
  }

  if (ma_device_start(&device) != MA_SUCCESS) {
    ma_device_uninit(&device);
    return false;
  }

  initialized = true;

  return true;
}

void SoundSystem::Cleanup() {
  if (!initialized) return;

  for (size_t i = 0; i < playing_count; ++i) {
    PlayingAudioClip* clip = playing_clips[i];
    ma_decoder_uninit(&clip->decoder);
    clip->next = free_clips;
    free_clips = clip;
  }

  playing_count = 0;

  ma_device_uninit(&device);

  initialized = false;
}

void SoundSystem::Play(AudioType type) {
  if (!initialized) return;

  AudioClip clip = database.GetClip(type);

  if (clip.IsLoaded()) {
    PlayClip(clip);
  }
}

void SoundSystem::PlayClip(const AudioClip& clip) {
  if (!initialized) return;

  // TODO: Evict old playing sounds if it ever reaches cap
  std::lock_guard<std::mutex> guard(clip_mutex);

  if (playing_count >= NULLSPACE_ARRAY_SIZE(playing_clips)) return;
  if (!clip.IsLoaded()) return;

  PlayingAudioClip* playing_clip = free_clips;
  ArenaSnapshot snapshot = database.perm_arena.GetSnapshot();

  if (!playing_clip) {
    // Allocate new playing clip if none available in free list.
    playing_clip = free_clips = memory_arena_push_type(&database.perm_arena, PlayingAudioClip);
  }

  free_clips = free_clips->next;

  ma_decoder* decoder = &playing_clip->decoder;
  ma_result result = ma_decoder_init_memory(clip.data, clip.size, &cfg_decoder, decoder);

  if (result != MA_SUCCESS) {
    log_error("Failed to play audio clip.\n");
    database.perm_arena.Revert(snapshot);
  } else {
    playing_clips[playing_count++] = playing_clip;
  }
}

SoundDatabase::SoundDatabase(MemoryArena& perm_arena) : perm_arena(perm_arena) { memset(clips, 0, sizeof(clips)); }

const char* kClipFilenames[] = {
    "",
    "sound/gun1.wa2",
    "sound/gun2.wa2",
    "sound/gun3.wa2",
    "sound/gun4.wa2",

    "sound/bomb1.wa2",
    "sound/bomb2.wa2",
    "sound/bomb3.wa2",
    "sound/bomb4.wa2",
    "sound/ebomb1.wa2",
    "sound/ebomb2.wa2",
    "sound/ebomb3.wa2",
    "sound/ebomb4.wa2",

    "sound/repel.wa2",
    "sound/decoy.wa2",
    "sound/burst.wa2",
    "sound/thor.wa2",

    "sound/multion.wa2",
    "sound/multioff.wa2",
    "sound/stealth.wa2",
    "sound/cloak.wa2",
    "sound/xradar.wa2",
    "sound/antiwarp.wa2",
    "sound/off.wa2",

    "sound/prize.wa2",
};

// TODO: Rework this entire thing. This is just the minimal implementation. It should be threaded so it doesn't cause
// hiccups. Maybe also use a cache that expires so old sounds don't sit around in memory.
AudioClip SoundDatabase::GetClip(AudioType type) {
  if (type == AudioType::None) return AudioClip{};

  size_t index = (size_t)type;
  AudioClip* clip = clips + index;

  // Check if the clip has been loaded before so it doesn't keep trying to load it.
  if (!(clip->flags & AudioClipFlag_Loaded)) {
    *clip = LoadClip(kClipFilenames[index]);
  }

  return *clip;
}

AudioClip SoundDatabase::LoadClip(const char* filename) {
  AudioClip result = {};

  result.data = asset_loader_arena(perm_arena, filename, &result.size);

  if (result.data) {
    result.flags |= AudioClipFlag_Loaded;
    printf("Loaded audio file %s\n", filename);
  } else {
    log_error("Failed to read audio file %s\n", filename);
  }

  return result;
}

}  // namespace null
