#ifndef NULLSPACE_PLATFORM_H_
#define NULLSPACE_PLATFORM_H_

#include <cstdlib>

namespace null {

struct MemoryArena;

typedef void (*ErrorLogger)(const char* fmt, ...);
typedef const char* (*StoragePathGetter)(MemoryArena& temp_arena, const char* path);
typedef unsigned char* (*AssetLoader)(const char* filename, size_t* size);
typedef unsigned char* (*AssetLoaderArena)(MemoryArena& arena, const char* filename, size_t* size);
typedef bool (*FolderCreate)(const char* path);
typedef void (*ClipboardPaste)(char* dest, size_t available_size);

struct Platform {
  ErrorLogger LogError;
  StoragePathGetter GetStoragePath;
  AssetLoader LoadAsset;
  AssetLoaderArena LoadAssetArena;

  FolderCreate CreateFolder;
  ClipboardPaste PasteClipboard;
};
extern Platform platform;

int null_stricmp(const char* s1, const char* s2);

}  // namespace null

#endif
