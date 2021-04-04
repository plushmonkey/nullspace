#ifndef NULLSPACE_MAP_H_
#define NULLSPACE_MAP_H_

#include "Memory.h"
#include "Types.h"

namespace null {

struct Tile {
  u32 x : 12;
  u32 y : 12;
  u32 tile : 8;
};

using TileId = u32;

struct Map {
  bool Load(MemoryArena& arena, const char* filename);

  bool IsSolid(u16 x, u16 y) const;
  TileId GetTileId(u16 x, u16 y) const;

  char filename[1024];
  char* data = nullptr;
  Tile* tiles = nullptr;
};

}  // namespace null

#endif
