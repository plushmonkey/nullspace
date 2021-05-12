#ifndef NULLSPACE_PLATFORM_H_
#define NULLSPACE_PLATFORM_H_

#include <cstdlib>

namespace null {

typedef void(*ErrorLogger)(const char* fmt, ...);

struct MemoryArena;

typedef const char* (*StoragePathGetter)(MemoryArena& temp_arena, const char* path);
extern StoragePathGetter GetStoragePath;

bool CreateFolder(const char* path);
void PasteClipboard(char* dest, size_t available_size);

int null_stricmp(const char* s1, const char* s2);

extern ErrorLogger log_error;

}  // namespace null

#endif
