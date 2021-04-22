#ifndef NULLSPACE_LVZCONTROLLER_H_
#define NULLSPACE_LVZCONTROLLER_H_

#include "Types.h"

namespace null {

struct FileRequester;
struct MemoryArena;
struct PacketDispatcher;
struct SpriteRenderer;

struct LvzController {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  FileRequester& requester;
  SpriteRenderer& renderer;

  LvzController(MemoryArena& perm_arena, MemoryArena& temp_arena, FileRequester& requester, SpriteRenderer& renderer,
                PacketDispatcher& dispatcher);

  void OnMapInformation(u8* pkt, size_t size);
  void OnFileDownload(struct FileRequest* request, u8* data);

 private:
  void ProcessGraphicFile(const char* filename, u8* data, size_t size);
};

}  // namespace null

#endif
