#include "TextureMap.h"

#include <cassert>
#include <cstring>

#include "../Memory.h"

namespace null {

u32 Hash(const char* str) {
  u32 hash = 5381;
  char c;

  while (c = *str++) {
    hash = hash * 33 ^ c;
  }

  return hash;
}

TextureMap::TextureMap(MemoryArena& arena) : arena(arena), free(nullptr) {
  for (size_t i = 0; i < kTextureMapBuckets; ++i) {
    elements[i] = nullptr;
  }

  for (size_t i = 0; i < 32; ++i) {
    Element* element = memory_arena_push_type(&arena, Element);
    element->next = free;
    free = element;
  }
}

void TextureMap::Insert(const char* name, u32 id, u32 width, u32 height) {
  u32 bucket = Hash(name) & (kTextureMapBuckets - 1);

  Element* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();
  }

  assert(strlen(name) < NULLSPACE_ARRAY_SIZE(element->name));

  strcpy(element->name, name);
  element->value.id = id;
  element->value.width = width;
  element->value.height = height;

  element->next = elements[bucket];
  elements[bucket] = element;
}

TextureData* TextureMap::Find(const char* name) {
  u32 bucket = Hash(name) & (kTextureMapBuckets - 1);
  Element* element = elements[bucket];

  while (element) {
    if (strcmp(element->name, name) == 0) {
      return &element->value;
    }
    element = element->next;
  }

  return nullptr;
}

TextureMap::Element* TextureMap::Allocate() {
  Element* result = nullptr;

  if (free) {
    result = free;
    free = free->next;
  } else {
    result = memory_arena_push_type(&arena, Element);
  }

  result->next = nullptr;
  return result;
}

}  // namespace null
