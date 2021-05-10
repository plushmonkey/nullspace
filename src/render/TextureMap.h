#ifndef NULLSPACE_TEXTUREMAP_H_
#define NULLSPACE_TEXTUREMAP_H_

#include "../Types.h"

namespace null {

struct MemoryArena;

struct TextureData {
  u32 id;
  u32 width;
  u32 height;
};

// Map texture name to texture data
constexpr size_t kTextureMapBuckets = (1 << 8);
struct TextureMap {
  struct Element {
    char name[64];
    TextureData value;

    Element* next;
  };

  MemoryArena& arena;
  Element* elements[kTextureMapBuckets];
  Element* free;

  TextureMap(MemoryArena& arena);

  void Insert(const char* name, u32 id, u32 width, u32 height);
  TextureData* Find(const char* name);

  void Clear();

 private:
  Element* Allocate();
};

}  // namespace null

#endif
