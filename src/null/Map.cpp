#include "Map.h"

#include <null/ArenaSettings.h>
#include <null/BrickManager.h>
#include <null/Clock.h>
#include <null/PlayerManager.h>
#include <null/net/Connection.h>
//
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace null {

constexpr int kFirstDoorId = 162;
constexpr int kLastDoorId = 169;

// TODO: Fast lookup map
bool IsSolid(TileId id) {
  if (id == 0) return false;
  if (id >= 162 && id <= 169) return true;
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

  // Maps are allocated in their own arena so they are freed automatically when the arena is reset
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

  size_t tile_count = (size - pos) / sizeof(Tile);
  Tile* tiles = (Tile*)(data + pos);

  this->door_count = GetTileCount(tiles, tile_count, kFirstDoorId, kLastDoorId);
  this->doors = memory_arena_push_type_count(&arena, Tile, this->door_count);

  for (size_t i = 0; i < kAnimatedTileCount; ++i) {
    animated_tiles[i].index = 0;
    animated_tiles[i].count = GetTileCount(tiles, tile_count, kAnimatedIds[i], kAnimatedIds[i]);
    animated_tiles[i].tiles = memory_arena_push_type_count(&arena, Tile, animated_tiles[i].count);
  }

  size_t door_index = 0;
  for (size_t tile_index = 0; tile_index < tile_count; ++tile_index) {
    Tile* tile = tiles + tile_index;

    this->tiles[tile->y * 1024 + tile->x] = tile->id;

    if (tile->id >= kFirstDoorId && tile->id <= kLastDoorId) {
      Tile* door = this->doors + door_index++;
      *door = *tile;
    }

    for (size_t i = 0; i < kAnimatedTileCount; ++i) {
      if (tile->id == kAnimatedIds[i]) {
        animated_tiles[i].tiles[animated_tiles[i].index++] = *tile;

        for (size_t j = 0; j < kAnimatedTileSizes[i]; ++j) {
          size_t y = tile->y + j;

          for (size_t k = 0; k < kAnimatedTileSizes[i]; ++k) {
            size_t x = tile->x + k;

            this->tiles[y * 1024 + x] = tile->id;
          }
        }
      }
    }
  }

  return true;
}

size_t Map::GetTileCount(Tile* tiles, size_t tile_count, TileId id_begin, TileId id_end) {
  size_t count = 0;

  for (size_t i = 0; i < tile_count; ++i) {
    Tile* tile = tiles + i;

    if (tile->id >= id_begin && tile->id <= id_end) {
      ++count;
    }
  }

  return count;
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

  PlayerManager* player_manager = nullptr;
  Player* self = nullptr;
  Vector2f self_min;
  Vector2f self_max;

  if (brick_manager) {
    player_manager = &brick_manager->player_manager;

    if (player_manager) {
      self = player_manager->GetSelf();

      if (self) {
        float radius = player_manager->connection.settings.ShipSettings[self->ship].GetRadius();

        self_min = self->position - Vector2f(radius, radius);
        self_max = self->position + Vector2f(radius, radius);
      }
    }
  }

  for (size_t i = 0; i < door_count; ++i) {
    Tile* door = doors + i;

    u8 id = table[door->id - kFirstDoorId];

    constexpr TileId kOpenDoorId = kLastDoorId + 1;

    TileId previous_id = tiles[door->y * 1024 + door->x];
    tiles[door->y * 1024 + door->x] = id;

    // If the tile just changed from open to closed then check for collisions
    if (self && previous_id == kOpenDoorId && id != kOpenDoorId) {
      Vector2f door_position((float)door->x, (float)door->y);

      // Perform door warp on overlap
      if (self && BoxBoxOverlap(self_min, self_max, door_position, door_position + Vector2f(1, 1))) {
        player_manager->Spawn(false);
      }
    }
  }
}

bool Map::CanFit(const Vector2f& position, float radius, u32 frequency) {
  for (float y_offset_check = -radius; y_offset_check < radius; ++y_offset_check) {
    for (float x_offset_check = -radius; x_offset_check < radius; ++x_offset_check) {
      if (IsSolid((u16)(position.x + x_offset_check), (u16)(position.y + y_offset_check), frequency)) {
        return false;
      }
    }
  }

  return true;
}

bool Map::IsColliding(const Vector2f& position, float radius, u32 frequency) const {
  s16 start_x = (s16)(position.x - radius - 1);
  s16 start_y = (s16)(position.y - radius - 1);

  s16 end_x = (s16)(position.x + radius + 1);
  s16 end_y = (s16)(position.y + radius + 1);

  if (start_x < 0) start_x = 0;
  if (start_y < 0) start_y = 0;

  if (end_x > 1023) end_x = 1023;
  if (end_y > 1023) end_y = 1023;

  for (s16 y = start_y; y <= end_y; ++y) {
    for (s16 x = start_x; x <= end_x; ++x) {
      if (!IsSolid(x, y, frequency)) continue;

      Rectangle tile_collider(Vector2f((float)x, (float)y), Vector2f((float)x + 1, (float)y + 1));
      Rectangle minkowski_collider = tile_collider.Grow(Vector2f(radius, radius));

      if (minkowski_collider.ContainsInclusive(position)) {
        return true;
      }
    }
  }

  return false;
}

TileId Map::GetTileId(u16 x, u16 y) const {
  if (!tiles) return 0;
  if (x >= 1024 || y >= 1024) return 20;

  return tiles[y * 1024 + x];
}

void Map::SetTileId(u16 x, u16 y, TileId id) {
  if (!tiles) return;
  if (x >= 1024 || y >= 1024) return;

  tiles[y * 1024 + x] = id;
}

TileId Map::GetTileId(const Vector2f& position) const {
  return GetTileId((u16)position.x, (u16)position.y);
}

bool Map::IsSolid(u16 x, u16 y, u32 frequency) const {
  TileId id = GetTileId(x, y);

  if (id == 250 && brick_manager) {
    Brick* brick = brick_manager->GetBrick(x, y);

    assert(brick);

    if (brick && brick->team == frequency) {
      return false;
    }
  }

  return null::IsSolid(id);
}

u32 Map::GetChecksum(u32 key) const {
  constexpr u32 kTileStart = 1;
  constexpr u32 kTileEnd = 160;

  int basekey = key;

  for (int y = basekey % 32; y < 1024; y += 32) {
    for (int x = basekey % 31; x < 1024; x += 31) {
      u8 tile = (u8)GetTileId(x, y);

      if (tile == 250) {
        tile = 0;
      }

      if ((tile >= kTileStart && tile <= kTileEnd) || tile == kTileSafeId) {
        key += basekey ^ tile;
      }
    }
  }

  return key;
}

CastResult Map::Cast(const Vector2f& from, const Vector2f& direction, float max_distance, u32 frequency) {
  CastResult result;

  result.hit = false;

  Vector2f unit_step(sqrt(1 + (direction.y / direction.x) * (direction.y / direction.x)),
                     sqrt(1 + (direction.x / direction.y) * (direction.x / direction.y)));

  Vector2f check = Vector2f(floorf(from.x), floorf(from.y));
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

    if (IsSolid((unsigned short)floorf(check.x), (unsigned short)floorf(check.y), frequency)) {
      result.hit = true;
      result.distance = clear_distance;
      break;
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
