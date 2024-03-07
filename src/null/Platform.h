#ifndef NULLSPACE_PLATFORM_H_
#define NULLSPACE_PLATFORM_H_

#include <stdlib.h>

namespace null {

struct MemoryArena;

typedef void (*LogFunction)(const char* fmt, ...);
typedef const char* (*StoragePathGetter)(MemoryArena& temp_arena, const char* path);
typedef unsigned char* (*AssetLoader)(const char* filename, size_t* size);
typedef unsigned char* (*AssetLoaderArena)(MemoryArena& arena, const char* filename, size_t* size);
typedef bool (*FolderCreate)(const char* path);
typedef void (*ClipboardPaste)(char* dest, size_t available_size);

typedef unsigned int (*MachineIdGet)();
typedef int (*TimeZoneBiasGet)();

struct Platform {
  LogFunction Log;
  LogFunction LogError;
  StoragePathGetter GetStoragePath;
  AssetLoader LoadAsset;
  AssetLoaderArena LoadAssetArena;

  FolderCreate CreateFolder;
  ClipboardPaste PasteClipboard;
  MachineIdGet GetMachineId;
  TimeZoneBiasGet GetTimeZoneBias;
};
extern Platform platform;

int null_stricmp(const char* s1, const char* s2);

}  // namespace null

#endif
