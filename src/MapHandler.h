#ifndef NULLSPACE_MAPHANDLER_H_
#define NULLSPACE_MAPHANDLER_H_

#include "Map.h"
#include "Memory.h"
#include "Types.h"

namespace null {

struct Connection;

struct MapHandler {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;

  char filename[20] = {};
  Map map;
  u32 checksum = 0;

  MapHandler(MemoryArena& perm_arena, MemoryArena& temp_arena) : perm_arena(perm_arena), temp_arena(temp_arena) {}

  bool OnMapInformation(Connection& connection, u8* pkt, size_t size);
  bool OnCompressedMap(Connection& connection, u8* pkt, size_t size);
};

}  // namespace null

#endif
