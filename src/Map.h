#ifndef NULLSPACE_MAP_H_
#define NULLSPACE_MAP_H_

#include "Memory.h"
#include "Types.h"
#include "Math.h"

namespace null {

struct Tile {
  u32 x : 12;
  u32 y : 12;
  u32 tile : 8;
};

struct CastResult {
  bool hit;
  float distance;
  Vector2f position;
  Vector2f normal;
};

using TileId = u32;

struct Map {
  bool Load(MemoryArena& arena, const char* filename);

  bool IsSolid(u16 x, u16 y) const;
  TileId GetTileId(u16 x, u16 y) const;

  u32 GetChecksum(u32 key) const;

  CastResult Cast(const Vector2f& from, const Vector2f& direction, float max_distance);

  char filename[1024];
  char* data = nullptr;
  Tile* tiles = nullptr;
};

}  // namespace null

#endif
