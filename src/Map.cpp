#include "Map.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "ArenaSettings.h"
#include "Tick.h"

namespace null {

constexpr int kFirstDoorId = 162;
constexpr int kLastDoorId = 169;

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
  tiles = arena.Allocate(1024 * 1024);

  assert(data);
  assert(tiles);

  fread(data, 1, size, file);
  fclose(file);

  size_t pos = 0;

  if (data[0] == 'B' && data[1] == 'M') {
    pos = *(u32*)(data + 2);
  }

  memset(tiles, 0, 1024 * 1024);

  // Expand tile data out into full grid
  size_t start = pos;
  door_count = 0;
  this->doors = (Tile*)arena.Allocate(0);

  while (pos < size) {
    Tile* tile = (Tile*)(data + pos);

    tiles[tile->y * 1024 + tile->x] = tile->id;
    if (tile->id >= kFirstDoorId && tile->id <= kLastDoorId) {
      // Allocate just enough space for one more door. This will increase the size of the already set door pointer.
      arena.Allocate(sizeof(Tile), 1);

      Tile* door = doors + door_count++;
      *door = *tile;
    }

    pos += sizeof(Tile);
  }

  return true;
}

void Map::UpdateDoors(const ArenaSettings& settings) {
  u32 current_tick = GetCurrentTick();

  // Check if we received any settings first
  if (settings.Type == 0) return;

  s32 count = TICK_DIFF(current_tick, last_seed_tick);

  if (settings.DoorDelay > 0) {
    count /= settings.DoorDelay;
  }

  for (s32 i = 0; i < count; ++i) {
    u8 seed = door_rng.seed;

    if (settings.DoorMode == -2) {
      seed = door_rng.GetNext();
    } else if (settings.DoorMode == -1) {
      u32 table[7];

      for (size_t j = 0; j < 7; ++j) {
        table[j] = door_rng.GetNext();
      }

      table[6] &= 0x8000000F;
      if ((s32)table[6] < 0) {
        table[6] = ((table[6] - 1) | 0xFFFFFFF0) + 1;
      }
      table[6] = -(s32)(table[6] != 0) & 0x80;

      table[5] &= 0x80000007;
      if ((s32)table[5] < 0) {
        table[5] = ((table[5] - 1) | 0xFFFFFFF8) + 1;
      }
      table[5] = -(s32)(table[5] != 0) & 0x40;

      table[4] &= 0x80000003;
      if ((s32)table[4] < 0) {
        table[4] = ((table[4] - 1) | 0xFFFFFFFC) + 1;
      }
      table[4] = -(s32)(table[4] != 0) & 0x20;

      table[3] &= 0x8000000F;
      if ((s32)table[3] < 0) {
        table[3] = ((table[3] - 1) | 0xFFFFFFF0) + 1;
      }
      table[3] = -(s32)(table[3] != 0) & 0x8;

      table[2] &= 0x80000007;
      if ((s32)table[2] < 0) {
        table[2] = ((table[2] - 1) | 0xFFFFFFF8) + 1;
      }
      table[2] = -(s32)(table[2] != 0) & 0x4;

      table[1] &= 0x80000003;
      if ((s32)table[1] < 0) {
        table[1] = ((table[1] - 1) | 0xFFFFFFFC) + 1;
      }
      table[1] = -(s32)(table[1] != 0) & 0x2;

      table[0] &= 0x80000001;
      if ((s32)table[0] < 0) {
        table[0] = ((table[0] - 1) | 0xFFFFFFFE) + 1;
      }
      table[0] = -(s32)(table[0] != 0) & 0x11;

      seed = table[6] + table[5] + table[4] + table[3] + table[2] + table[1] + table[0];
    } else if (settings.DoorMode >= 0) {
      seed = (u8)settings.DoorMode;
    }

    SeedDoors(seed);
    last_seed_tick = current_tick;
  }
}

void Map::SeedDoors(u32 seed) {
  u8 bottom = seed & 0xFF;
  u8 table[8];

  table[0] = ((~bottom & 1) << 3) | 0xA2;
  table[1] = (-((bottom & 2) != 0) & 0xF9) + 0xAA;
  table[2] = (-((bottom & 4) != 0) & 0xFA) + 0xAA;
  table[3] = (-((bottom & 8) != 0) & 0xFB) + 0xAA;
  table[4] = (-((bottom & 0x10) != 0) & 0xFC) + 0xAA;
  table[5] = (-((bottom & 0x20) != 0) & 0xFD) + 0xAA;
  table[6] = (~(bottom >> 5) & 2) | 0xA8;
  table[7] = 0xAA - ((bottom & 0x80) != 0);

  for (size_t i = 0; i < door_count; ++i) {
    Tile* door = doors + i;

    u8 id = table[door->id - kFirstDoorId];
    tiles[door->y * 1024 + door->x] = id;
  }
}

TileId Map::GetTileId(u16 x, u16 y) const {
  if (!tiles) return 0;
  if (x >= 1024 || y >= 1024) return 0;

  return tiles[y * 1024 + x];
}

bool Map::IsSolid(u16 x, u16 y) const {
  TileId id = GetTileId(x, y);

  return null::IsSolid(id);
}

u32 Map::GetChecksum(u32 key) const {
  constexpr u32 kTileStart = 1;
  constexpr u32 kTileEnd = 160;

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

CastResult Map::Cast(const Vector2f& from, const Vector2f& direction, float max_distance) {
  CastResult result;

  result.hit = false;

  Vector2f unit_step(sqrt(1 + (direction.y / direction.x) * (direction.y / direction.x)),
                     sqrt(1 + (direction.x / direction.y) * (direction.x / direction.y)));

  Vector2f check = Vector2f(std::floor(from.x), std::floor(from.y));
  Vector2f travel;

  Vector2f step;

  if (direction.x < 0) {
    step.x = -1.0f;
    travel.x = (from.x - check.x) * unit_step.x;
  } else {
    step.x = 1.0f;
    travel.x = (check.x + 1 - from.x) * unit_step.x;
  }

  if (direction.y < 0) {
    step.y = -1.0f;
    travel.y = (from.y - check.y) * unit_step.y;
  } else {
    step.y = 1.0f;
    travel.y = (check.y + 1 - from.y) * unit_step.y;
  }

  float distance = 0.0f;

  while (distance < max_distance) {
    // Walk along shortest path
    float clear_distance = distance;

    if (travel.x < travel.y) {
      check.x += step.x;
      distance = travel.x;
      travel.x += unit_step.x;
    } else {
      check.y += step.y;
      distance = travel.y;
      travel.y += unit_step.y;
    }

    if (check.x >= 0 && check.x < 1024 && check.y >= 0 && check.y < 1024) {
      if (IsSolid((unsigned short)check.x, (unsigned short)check.y)) {
        result.hit = true;
        result.distance = clear_distance;
        break;
      }
    }
  }

  if (result.hit) {
    float dist;

    bool intersected = RayBoxIntersect(from, direction, check, Vector2f(1, 1), &dist, &result.normal);

    if (!intersected || dist > max_distance) {
      result.hit = false;
      result.position = from + direction * max_distance;
      result.distance = max_distance;
    } else {
      result.distance = dist;
      result.position = from + direction * dist;
    }
  } else {
    result.hit = false;
    result.distance = max_distance;
    result.position = from + direction * max_distance;
  }

  return result;
}

}  // namespace null
