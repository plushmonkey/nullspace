#include "Platform.h"

#include <stdarg.h>

#include "Memory.h"
#include "render/Graphics.h"

#ifdef _WIN32
#ifdef APIENTRY
// Fix warning with glad definition
#undef APIENTRY
#endif

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
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

  vfprintf(stderr, fmt, args);

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

unsigned int GetMachineId() {
  char file_system_name[64];
  DWORD file_system_flags;
  DWORD max_component_size;
  DWORD volume_serial_number;
  char volume_name[256];
  char windows_directory[256];

  BOOL result = GetVolumeInformationA("c:\\", volume_name, 256, &volume_serial_number, &max_component_size,
                                      &file_system_flags, file_system_name, 64);

  if (result) {
    return volume_serial_number;
  }

  GetWindowsDirectoryA(windows_directory, 256);

  result = GetVolumeInformationA(windows_directory, volume_name, 256, &volume_serial_number, &max_component_size,
                                 &file_system_flags, file_system_name, 64);

  if (result) {
    return volume_serial_number;
  }

  return rand();
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

Platform platform = {StandardLog,  StandardGetStoragePath, StandardLoadAsset, StandardLoadAssetArena,
                     CreateFolder, PasteClipboard,         GetMachineId,      GetTimeZoneBias};

}  // namespace null
