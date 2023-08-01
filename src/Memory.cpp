#include "Memory.h"

#include <assert.h>

#include <cstdio>

#include "Logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#define MONITOR_PERM_ALLOCATIONS 0

namespace null {

MemoryArena::MemoryArena(u8* memory, size_t max_size) : base(memory), current(memory), max_size(max_size) {}

u8* MemoryArena::Allocate(size_t size, size_t alignment) {
  assert(alignment > 0);

  size_t adj = alignment - 1;
  u8* result = (u8*)(((size_t)this->current + adj) & ~adj);
  this->current = result + size;

#if MONITOR_PERM_ALLOCATIONS
  if (this == perm_global) {
    size_t allocated = (size_t)(this->current - this->base);

    Log(LogLevel::Debug, "Allocating %zd with align %zd in perm arena. (allocated: %zd)", size, alignment, allocated);
  }
#endif

  assert(this->current <= this->base + this->max_size);

  return result;
}

MemoryArena MemoryArena::CreateArena(size_t size, size_t alignment) {
  u8* base = Allocate(size, alignment);
  assert(base);
  return MemoryArena(base, size);
}

void MemoryArena::Reset() {
  this->current = this->base;
}

u8* AllocateMirroredBuffer(size_t size) {
#ifdef _WIN32
  SYSTEM_INFO sys_info = {0};

  GetSystemInfo(&sys_info);

  size_t granularity = sys_info.dwAllocationGranularity;

  if (((size / granularity) * granularity) != size) {
    Log(LogLevel::Error, "Incorrect size. Must be multiple of %zd", granularity);
    return 0;
  }

  HANDLE map_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, (DWORD)size * 2, NULL);

  if (map_handle == NULL) {
    return 0;
  }

  u8* buffer = (u8*)VirtualAlloc(NULL, size * 2, MEM_RESERVE, PAGE_READWRITE);

  if (buffer == NULL) {
    CloseHandle(map_handle);
    return 0;
  }

  VirtualFree(buffer, 0, MEM_RELEASE);

  u8* view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer);

  if (view == NULL) {
    u32 error = GetLastError();
    CloseHandle(map_handle);
    return 0;
  }

  u8* mirror_view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer + size);

  if (mirror_view == NULL) {
    u32 error = GetLastError();

    UnmapViewOfFile(view);
    CloseHandle(map_handle);
    return 0;
  }

  return view;
#else
  return nullptr;
#endif
}

}  // namespace null
