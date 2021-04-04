#include "Map.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace null {

// TODO: Fast lookup map and handle doors according to door seed
bool IsSolid(TileId id) {
  if (id == 0) return false;
  if (id >= 162 && id <= 169) return false;
  if (id < 170) return true;
  if (id >= 192 && id <= 240) return true;
  if (id >= 242 && id <= 252) return true;

  return false;
}

bool Map::Load(MemoryArena& arena, const char* filename) {
  assert(strlen(filename) < 1024);

  strcpy(this->filename, filename);

  FILE* file = fopen(filename, "rb");

  if (!file) {
    return false;
  }

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size <= 0) {
    fclose(file);
    return false;
  }

  // TODO: freeing

  data = (char*)arena.Allocate(size);
  tiles = memory_arena_push_type_count(&arena, Tile, 1024 * 1024);

  assert(data);
  assert(tiles);

  fread(data, 1, size, file);
  fclose(file);

  size_t pos = 0;

  if (data[0] == 'B' && data[1] == 'M') {
    pos = *(u32*)(data + 2);
  }

  memset(tiles, 0, sizeof(Tile) * 1024 * 1024);

  // Expand tile data out into full grid
  while (pos < size) {
    Tile* tile = (Tile*)(data + pos);

    tiles[tile->y * 1024 + tile->x] = *tile;

    pos += sizeof(Tile);
  }

  return true;
}

TileId Map::GetTileId(u16 x, u16 y) const {
  if (!tiles) return 0;
  if (x >= 1024 || y >= 1024) return 0;

  return tiles[y * 1024 + x].tile;
}

bool Map::IsSolid(unsigned short x, unsigned short y) const {
  TileId id = GetTileId(x, y);

  return null::IsSolid(id);
}

u32 Map::GetChecksum(u32 key) {
  constexpr u32 kTileStart = 1;
  constexpr u32 kTileEnd = 160;
  constexpr u32 kTileSafe = 171;

  int basekey = key;

  for (int y = basekey % 32; y < 1024; y += 32) {
    for (int x = basekey % 31; x < 1024; x += 31) {
      u8 tile = (u8)GetTileId(x, y);
      if ((tile >= kTileStart && tile <= kTileEnd) || tile == kTileSafe) {
        key += basekey ^ tile;
      }
    }
  }

  return key;
}

}  // namespace null
