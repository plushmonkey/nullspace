#ifndef NULLSPACE_MAP_H_
#define NULLSPACE_MAP_H_

#include "Math.h"
#include "Memory.h"
#include "Random.h"
#include "Types.h"

namespace null {

struct Tile {
  u32 x : 12;
  u32 y : 12;
  u32 id : 8;
};

struct CastResult {
  bool hit;
  float distance;
  Vector2f position;
  Vector2f normal;
};

struct ArenaSettings;

using TileId = u8;
constexpr u32 kTileSafe = 171;

struct Map {
  bool Load(MemoryArena& arena, const char* filename);

  bool IsSolid(u16 x, u16 y) const;
  TileId GetTileId(u16 x, u16 y) const;

  void UpdateDoors(const ArenaSettings& settings);
  void SeedDoors(u32 seed);

  u32 GetChecksum(u32 key) const;

  CastResult Cast(const Vector2f& from, const Vector2f& direction, float max_distance);

  char filename[1024];
  u32 checksum = 0;
  VieRNG door_rng;
  u32 last_seed_tick = 0;
  u32 compressed_size = 0;
  char* data = nullptr;
  u8* tiles = nullptr;

  size_t door_count = 0;
  Tile* doors = nullptr;
};

}  // namespace null

#endif
