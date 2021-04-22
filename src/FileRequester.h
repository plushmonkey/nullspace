#ifndef NULLSPACE_FILEREQUESTER_H_
#define NULLSPACE_FILEREQUESTER_H_

#include "Types.h"

namespace null {

using RequestCallback = void (*)(void* user, struct FileRequest* request, u8* data);

struct FileRequest {
  RequestCallback callback;
  void* user;

  u16 decompress;
  u16 index;

  u32 size;
  u32 checksum;

  char filename[260];

  FileRequest* next;
};

struct Connection;
struct MemoryArena;
struct PacketDispatcher;

struct FileRequester {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  Connection& connection;

  FileRequest* current = nullptr;
  FileRequest* requests = nullptr;
  FileRequest* free = nullptr;

  FileRequester(MemoryArena& perm_arena, MemoryArena& temp_arena, Connection& connection, PacketDispatcher& dispatcher);

  // Immediately calls the callback function if the file already exists locally
  void Request(const char* filename, u16 index, u32 size, u32 checksum, bool decompress, RequestCallback callback,
               void* user);

  void OnCompressedFile(u8* pkt, size_t size);

 private:
  void SendNextRequest();
};

}  // namespace null

#endif
