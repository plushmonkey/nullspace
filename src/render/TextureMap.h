#ifndef NULLSPACE_TEXTUREMAP_H_
#define NULLSPACE_TEXTUREMAP_H_

#include <cassert>
#include <cstring>

#include "../HashMap.h"
#include "../Types.h"

namespace null {

struct TextureKey {
  char name[64];

  bool operator==(const TextureKey& other) { return strcmp(name, other.name) == 0; }
};

struct TextureData {
  u32 id;
  u32 width;
  u32 height;

  bool operator==(const TextureData& other) { return id == other.id && width == other.width && height == other.height; }
};

struct TextureHasher {
  inline u32 operator()(const TextureKey& key) { return Hash(key.name); }
  inline u32 operator()(const char* str) { return Hash(str); }

  inline u32 Hash(const char* str) {
    u32 hash = 5381;
    char c;

    while ((c = *str++)) {
      hash = hash * 33 ^ c;
    }

    return hash;
  }
};

struct TextureMap : public HashMap<TextureKey, TextureData, TextureHasher> {
  TextureMap(MemoryArena& arena) : HashMap<TextureKey, TextureData, TextureHasher>(arena) {}

  TextureData* Find(const char* str) {
    u32 bucket = hasher(str) & (NULLSPACE_ARRAY_SIZE(elements) - 1);
    Element* element = elements[bucket];

    while (element) {
      if (strcmp(element->key.name, str) == 0) {
        return &element->value;
      }

      element = element->next;
    }

    return nullptr;
  }

  void Insert(const char* name, u32 id, u32 width, u32 height) {
    TextureKey key;

    assert(strlen(name) < sizeof(key.name));
    if (strlen(name) > sizeof(key.name)) return;

    strcpy(key.name, name);

    TextureData data;
    data.id = id;
    data.width = width;
    data.height = height;

    HashMap::Insert(key, data);
  }
};
}  // namespace null
#endif
