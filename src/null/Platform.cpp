#include "Platform.h"

#include <null/Logger.h>
#include <null/Memory.h>
#include <null/render/Graphics.h>
//
#include <stdarg.h>

#ifdef _WIN32
#ifdef APIENTRY
// Fix warning with glad definition
#undef APIENTRY
#endif

#include <Windows.h>

#else
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>

#ifndef __ANDROID__
#include <GLFW/glfw3.h>

GLFWwindow* clipboard_window = nullptr;
#endif

#endif

namespace null {

void StandardLog(const char* fmt, ...) {
  va_list args;

  va_start(args, fmt);

  LogArgs(LogLevel::Info, fmt, args);

  va_end(args);
}

void ErrorLog(const char* fmt, ...) {
  va_list args;

  va_start(args, fmt);

  LogArgs(LogLevel::Error, fmt, args);

  va_end(args);
}

const char* StandardGetStoragePath(MemoryArena& temp_arena, const char* path) {
  return path;
}

u8* StandardLoadAsset(const char* filename, size_t* size) {
  FILE* f = fopen(filename, "rb");

  if (!f) return nullptr;

  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);

  u8* buffer = (u8*)malloc(*size);

  fread(buffer, 1, *size, f);

  fclose(f);

  return buffer;
}

u8* StandardLoadAssetArena(MemoryArena& arena, const char* filename, size_t* size) {
  FILE* f = fopen(filename, "rb");

  if (!f) {
    *size = 0;
    return nullptr;
  }

  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);

  ArenaSnapshot snapshot = arena.GetSnapshot();
  u8* buffer = arena.Allocate(*size);

  if (fread(buffer, 1, *size, f) != *size) {
    arena.Revert(snapshot);
  }

  fclose(f);

  return buffer;
}

#ifdef _WIN32

bool CreateFolder(const char* path) {
  return CreateDirectory(path, NULL);
}

inline bool IsValidCharacter(unsigned short c) {
  return (c >= ' ' && c <= '~') || c == 0xDF;
}

// Converts to Windows-1252 character set
inline bool IsForeignCharacter(unsigned short c, unsigned char* out) {
  switch (c) {
    case 0x160: {
      *out = 0x8A;
      return true;
    } break;
    case 0x161: {
      *out = 0x9A;
      return true;
    } break;
    case 0x178: {
      *out = 0x9F;
      return true;
    } break;
    case 0x17D: {
      *out = 0x8E;
      return true;
    } break;
    case 0x17E: {
      *out = 0x9E;
      return true;
    } break;
    case 0x20AC: {
      *out = 0x80;
      return true;
    } break;
  }

  if (c >= 0xC0 && c <= 0xFF) {
    *out = (unsigned char)c;
    return true;
  }

  return false;
}

void PasteClipboard(char* dest, size_t available_size) {
  if (OpenClipboard(NULL)) {
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
      HANDLE handle = GetClipboardData(CF_UNICODETEXT);
      unsigned short* data = (unsigned short*)GlobalLock(handle);

      if (data) {
        for (size_t i = 0; i < available_size && data[i]; ++i, ++dest) {
          unsigned char fcode = 0;

          if (null::IsValidCharacter(data[i])) {
            *dest = (char)data[i];
          } else if (null::IsForeignCharacter(data[i], &fcode)) {
            *dest = fcode;
          } else {
            *dest = '?';
          }
        }
        *dest = 0;

        GlobalUnlock(data);
      }
    }

    CloseClipboard();
  }
}

u32 GetDriveSerial(const char* root) {
  char file_system_name[64];
  DWORD file_system_flags;
  DWORD max_component_size;
  DWORD volume_serial_number = 0;
  char volume_name[256];

  BOOL result = GetVolumeInformationA(root, volume_name, 256, &volume_serial_number, &max_component_size,
                                      &file_system_flags, file_system_name, 64);

  if (result && volume_serial_number > 0 && volume_serial_number != 0xFFFFFFFF) {
    return volume_serial_number;
  }

  return 0;
}

unsigned int GetMachineId() {
  HW_PROFILE_INFO profile_info = {};

  if (GetCurrentHwProfileA(&profile_info)) {
    auto guid = profile_info.szHwProfileGuid;

    // Grab the last 8 digits of the guid
    u32 machine_id = strtol(guid + HW_PROFILE_GUIDLEN - 10, nullptr, 16);

    if (machine_id != 0 && machine_id != 0xFFFFFFFF) {
      return machine_id;
    }
  }

  // Fallback to trying to get the volume serial from the C drive, Windows directory drive, or cwd drive.
  u32 machine_id = GetDriveSerial("C:\\");
  if (machine_id != 0) return machine_id;

  char windows_directory[256];
  if (GetWindowsDirectoryA(windows_directory, 256)) {
    machine_id = GetDriveSerial(windows_directory);
    if (machine_id != 0) return machine_id;
  }

  machine_id = GetDriveSerial(NULL);
  if (machine_id != 0) return machine_id;

  // Fallback to a random number that might be within acceptable range.
  return (rand() % 0x6FFF0000) + 0xFFFF;
}

int GetTimeZoneBias() {
  TIME_ZONE_INFORMATION tzi;

  DWORD result = GetTimeZoneInformation(&tzi);

  if (result == 0) {
    return 0;
  } else if (result == 2) {
    return tzi.DaylightBias + tzi.Bias;
  }

  return tzi.Bias;
}

int null_stricmp(const char* s1, const char* s2) {
  return _stricmp(s1, s2);
}

#else
bool CreateFolder(const char* path) {
  return mkdir(path, 0700) == 0;
}
void PasteClipboard(char* dest, size_t available_size) {
#ifndef __ANDROID__
  const char* clipboard = glfwGetClipboardString(clipboard_window);
  if (clipboard) {
    for (size_t i = 0; i < available_size && *clipboard && *clipboard != 10; ++i) {
      *dest++ = *clipboard++;
    }
    *dest = 0;
  }
#endif
}

int null_stricmp(const char* s1, const char* s2) {
  return strcasecmp(s1, s2);
}

unsigned int GetMachineId() {
  return rand();
}

int GetTimeZoneBias() {
  return 240;
}

#endif

Platform platform = {StandardLog,  ErrorLog,       StandardGetStoragePath, StandardLoadAsset, StandardLoadAssetArena,
                     CreateFolder, PasteClipboard, GetMachineId,           GetTimeZoneBias};

}  // namespace null
