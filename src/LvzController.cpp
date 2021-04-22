#include "LvzController.h"

#include <cstdio>

#include "Buffer.h"
#include "FileRequester.h"
#include "Inflate.h"
#include "Memory.h"
#include "Platform.h"
#include "net/PacketDispatcher.h"
#include "render/Graphics.h"
#include "render/Image.h"
#include "render/SpriteRenderer.h"

namespace null {

constexpr u32 kMagicValue = 0x544E4F43;  // CONT

struct LvzHeader {
  u32 magic;  // CONT
  u32 section_count;
};

struct LvzSectionHeader {
  u32 magic;  // CONT
  u32 decompressed_size;
  u32 timestamp;
  u32 compressed_size;
};

static void OnMapInformationPkt(void* user, u8* pkt, size_t size) {
  LvzController* controller = (LvzController*)user;

  controller->OnMapInformation(pkt, size);
}

static void OnDownload(void* user, FileRequest* request, u8* data) {
  LvzController* controller = (LvzController*)user;

  controller->OnFileDownload(request, data);
}

LvzController::LvzController(MemoryArena& perm_arena, MemoryArena& temp_arena, FileRequester& requester,
                             SpriteRenderer& renderer, PacketDispatcher& dispatcher)
    : perm_arena(perm_arena), temp_arena(temp_arena), requester(requester), renderer(renderer) {
  dispatcher.Register(ProtocolS2C::MapInformation, OnMapInformationPkt, this);
}

void LvzController::OnMapInformation(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // 0x29

  // Skip map file
  buffer.ReadString(24);

  u16 index = 1;
  while (buffer.read < buffer.write) {
    char* filename = buffer.ReadString(16);
    u32 checksum = buffer.ReadU32();
    u32 filesize = buffer.ReadU32();

    requester.Request(filename, index++, filesize, checksum, false, OnDownload, this);
  }
}

void LvzController::OnFileDownload(struct FileRequest* request, u8* data) {
  LvzHeader* lvz_header = (LvzHeader*)data;

  if (lvz_header->magic != kMagicValue) {
    printf("Received lvz file %s that didn't contain lvz format.\n", request->filename);
    return;
  }

  printf("Lvz section count: %d\n", lvz_header->section_count);

  u8* ptr = data + sizeof(LvzHeader);

  for (size_t i = 0; i < lvz_header->section_count; ++i) {
    LvzSectionHeader* section_header = (LvzSectionHeader*)ptr;
    ptr += sizeof(LvzSectionHeader);

    char* filename = (char*)ptr;
    while (*ptr++)
      ;
    printf("Section filename: %s\n", filename);

    u8* section_data = ptr;
    size_t size = section_header->compressed_size;

    if (section_header->decompressed_size != section_header->compressed_size) {
      mz_ulong decompressed_size = section_header->decompressed_size;
      section_data = temp_arena.Allocate(decompressed_size);
      int status = mz_uncompress(section_data, &decompressed_size, ptr, section_header->compressed_size);

      if (status != MZ_OK) {
        fprintf(stderr, "Failed to uncompress lvz data.\n");
        ptr += section_header->compressed_size;
        continue;
      }
      size = decompressed_size;
    }

    ptr += section_header->compressed_size;

    ProcessGraphicFile(filename, section_data, size);
  }

  printf("Lvz %s processed.\n", request->filename);
}

void LoadShip(SpriteRenderer& renderer, u8* data, size_t size, int ship) {
  int width, height;

  u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

  if (image_data) {
    int count = 0;
    float dim = width / 10.0f;
    SpriteRenderable* renderables = renderer.LoadSheetFromMemory(image_data, width, height, Vector2f(dim, dim), &count);

    for (size_t i = 0; i < 40; ++i) {
      Graphics::ship_sprites[i + ship * 40] = renderables[i];
    }

    ImageFree(image_data);
  }
}

// TODO: Build a system to handle this automatically.
// Just does ships for now
void LvzController::ProcessGraphicFile(const char* filename, u8* data, size_t size) {
  const char* extension = strstr(filename, ".");
  char extension_free[256];

  if (extension == nullptr) return;

  size_t name_size = extension - filename;

  memcpy(extension_free, filename, name_size);
  extension_free[name_size] = 0;

  if (null_stricmp(extension_free, "ships") == 0) {
    renderer.FreeSheet(Graphics::ship_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim = width / 10.0f;
      Graphics::ship_sprites = renderer.LoadSheetFromMemory(image_data, width, height, Vector2f(dim, dim), &count);
      ImageFree(image_data);
    }
  } else if (null_stricmp(extension_free, "ship1") == 0) {
    LoadShip(renderer, data, size, 0);
  } else if (null_stricmp(extension_free, "ship2") == 0) {
    LoadShip(renderer, data, size, 1);
  } else if (null_stricmp(extension_free, "ship3") == 0) {
    LoadShip(renderer, data, size, 2);
  } else if (null_stricmp(extension_free, "ship4") == 0) {
    LoadShip(renderer, data, size, 3);
  } else if (null_stricmp(extension_free, "ship5") == 0) {
    LoadShip(renderer, data, size, 4);
  } else if (null_stricmp(extension_free, "ship6") == 0) {
    LoadShip(renderer, data, size, 5);
  } else if (null_stricmp(extension_free, "ship7") == 0) {
    LoadShip(renderer, data, size, 6);
  } else if (null_stricmp(extension_free, "ship8") == 0) {
    LoadShip(renderer, data, size, 7);
  }
}

}  // namespace null
