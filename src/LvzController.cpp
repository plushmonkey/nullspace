#include "LvzController.h"

#include <cstdio>

#include "Buffer.h"
#include "FileRequester.h"
#include "Inflate.h"
#include "Memory.h"
#include "Platform.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/Image.h"
#include "render/SpriteRenderer.h"

// TODO: register for lvz packets and update objects

namespace null {

#define MAKE_MAGIC(c1, c2, c3, c4) (c1 | c2 << 8 | c3 << 16 | c4 << 24)

constexpr u32 kMagicValue = MAKE_MAGIC('C', 'O', 'N', 'T');
constexpr u32 kMagicVersion1 = MAKE_MAGIC('C', 'L', 'V', '1');
constexpr u32 kMagicVersion2 = MAKE_MAGIC('C', 'L', 'V', '2');

enum class DisplayMode { ShowAlways, EnterZone, EnterArena, Kill, Death, ServerControlled };

enum class ReferencePoint {
  Normal,
  ScreenCenter,
  BottomRight,
  StatBoxBottomRight,
  SpecialTopRight,
  EnergyBelow,
  ChatTopLeft,
  RadarTopLeft,
  RadarTextTopLeft,
  WeaponsTopLeft,
  WeaponsBottomLeft
};

#pragma pack(push, 1)
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

struct ObjectSectionHeader {
  u32 magic;  // CLV1 or CLV2
  u32 object_count;
  u32 image_count;
};

struct ObjectDefinition {
  u16 map_object : 1;
  u16 object_id : 15;

  union {
    s16 x_map;
    struct {
      u16 x_type : 4;
      s16 x_screen : 12;
    };
  };

  union {
    s16 y_map;
    struct {
      u16 y_type : 4;
      s16 y_screen : 12;
    };
  };

  u8 image_number;
  u8 layer;

  u16 display_time : 12;
  u16 display_mode : 4;
};

struct ImageDefinition {
  s16 x_count;
  s16 y_count;
  s16 animation_time;
};
#pragma pack(pop)

inline Layer GetLayer(u8 layer_num) {
  Layer layers[] = {Layer::BelowAll,   Layer::AfterBackground, Layer::AfterTiles, Layer::AfterWeapons,
                    Layer::AfterShips, Layer::AfterGauges,     Layer::AfterChat,  Layer::TopMost};

  return layers[layer_num];
}

Vector2f GetScreenReferencePoint(Camera& ui_camera, ReferencePoint point) {
  switch (point) {
    case ReferencePoint::Normal: {
      return Vector2f(0, 0);
    } break;
    case ReferencePoint::ScreenCenter: {
      return ui_camera.surface_dim * 0.5f;
    } break;
    case ReferencePoint::BottomRight: {
      return ui_camera.surface_dim;
    } break;
    // TODO: Implement the rest
    default: {
    } break;
  }

  return Vector2f(0, 0);
}

static void OnMapInformationPkt(void* user, u8* pkt, size_t size) {
  LvzController* controller = (LvzController*)user;

  controller->OnMapInformation(pkt, size);
}

static void OnLvzTogglePkt(void* user, u8* pkt, size_t size) {
  LvzController* controller = (LvzController*)user;

  controller->OnLvzToggle(pkt, size);
}

static void OnDownload(void* user, FileRequest* request, u8* data) {
  LvzController* controller = (LvzController*)user;

  controller->OnFileDownload(request, data);
}

LvzController::LvzController(MemoryArena& perm_arena, MemoryArena& temp_arena, FileRequester& requester,
                             SpriteRenderer& renderer, PacketDispatcher& dispatcher)
    : perm_arena(perm_arena), temp_arena(temp_arena), requester(requester), renderer(renderer) {
  dispatcher.Register(ProtocolS2C::MapInformation, OnMapInformationPkt, this);
  dispatcher.Register(ProtocolS2C::ToggleLVZ, OnLvzTogglePkt, this);
}

void LvzController::Update(float dt) {
  for (size_t i = 0; i < animation_count; ++i) {
    Animation* anim = animations + i;

    anim->t += dt;
  }
}

void LvzController::Render(Camera& ui_camera, Camera& game_camera) {
  for (size_t i = 0; i < active_screen_object_count; ++i) {
    LvzObject* obj = active_screen_objects[i];

    ReferencePoint reference_x = (ReferencePoint)obj->x_type;
    ReferencePoint reference_y = (ReferencePoint)obj->y_type;

    Vector2f base_x = GetScreenReferencePoint(ui_camera, reference_x);
    Vector2f base_y = GetScreenReferencePoint(ui_camera, reference_y);

    Vector2f position(base_x.x + (float)obj->x_screen, base_y.y + (float)obj->y_screen);
    Animation* anim = animations + obj->animation_index;

    renderer.Draw(ui_camera, anim->GetFrame(), position, obj->layer);
  }

  renderer.Render(ui_camera);

  for (size_t i = 0; i < active_map_object_count; ++i) {
    LvzObject* obj = active_map_objects[i];

    Vector2f position(obj->x_map / 16.0f, obj->y_map / 16.0f);
    Animation* anim = animations + obj->animation_index;

    renderer.Draw(game_camera, anim->GetFrame(), position, obj->layer);
  }

  renderer.Render(game_camera);
}

void LvzController::OnLvzToggle(u8* pkt, size_t size) {
  struct ObjToggle {
    u16 id : 15;
    u16 off : 1;
  };

  for (size_t i = 0; i < (size - 1) / sizeof(u16); ++i) {
    ObjToggle* toggle = (ObjToggle*)(pkt + 1 + i * sizeof(u16));

    if (toggle->off) {
      for (size_t j = 0; j < active_map_object_count; ++j) {
        if (active_map_objects[j]->object_id == toggle->id) {
          active_map_objects[j--] = active_map_objects[--active_map_object_count];
        }
      }

      for (size_t j = 0; j < active_screen_object_count; ++j) {
        if (active_screen_objects[j]->object_id == toggle->id) {
          active_screen_objects[j--] = active_screen_objects[--active_screen_object_count];
        }
      }
    } else {
      for (size_t i = 0; i < object_count; ++i) {
        LvzObject* obj = objects + i;

        if (obj->object_id == toggle->id) {
          if (obj->map_object) {
            active_map_objects[active_map_object_count++] = obj;
          } else {
            active_screen_objects[active_screen_object_count++] = obj;
          }
          break;
        }
      }
    }
  }
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

  u8* ptr = data + sizeof(LvzHeader);

  for (size_t i = 0; i < lvz_header->section_count; ++i) {
    LvzSectionHeader* section_header = (LvzSectionHeader*)ptr;
    ptr += sizeof(LvzSectionHeader);

    char* filename = (char*)ptr;
    while (*ptr++)
      ;

    u8* section_data = ptr;
    size_t size = section_header->compressed_size;

    ArenaSnapshot snapshot = temp_arena.GetSnapshot();

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

    if (section_header->timestamp != 0 || *filename != 0) {
      int width, height;

      u8* image_data = ImageLoadFromMemory(section_data, size, &width, &height);
      GLuint texture_id = renderer.CreateTexture(filename, image_data, width, height);
      ImageFree(image_data);

      ProcessGraphicFile(filename, section_data, size);
    }

    temp_arena.Revert(snapshot);
  }

  // Loop again after processing the image textures to create the lvz objects
  ptr = data + sizeof(LvzHeader);
  for (size_t i = 0; i < lvz_header->section_count; ++i) {
    LvzSectionHeader* section_header = (LvzSectionHeader*)ptr;
    ptr += sizeof(LvzSectionHeader);

    char* filename = (char*)ptr;
    while (*ptr++)
      ;

    u8* section_data = ptr;
    size_t size = section_header->compressed_size;

    ArenaSnapshot snapshot = temp_arena.GetSnapshot();

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

    if (section_header->timestamp == 0 && *filename == 0) {
      ProcessObjects(section_data, size);
    }

    temp_arena.Revert(snapshot);
  }
}

void LvzController::ProcessObjects(u8* data, size_t size) {
  ObjectSectionHeader* header = (ObjectSectionHeader*)data;

  if (header->magic != kMagicVersion1 && header->magic != kMagicVersion2) return;

  u8* ptr = data + sizeof(ObjectSectionHeader);

  // Skip over object definitions first to simplify image creation
  ptr += sizeof(ObjectDefinition) * header->object_count;

  size_t animation_base = animation_count;

  for (u32 i = 0; i < header->image_count; ++i) {
    ImageDefinition* image = (ImageDefinition*)ptr;

    ptr += sizeof(ImageDefinition);

    char* filename = (char*)ptr;

    while (*ptr++)
      ;

    TextureData* texture_data = renderer.texture_map->Find(filename);

    if (texture_data) {
      AnimatedSprite* sprite = sprites + animation_count;
      Animation* anim = animations + animation_count++;

      int width = texture_data->width;
      int height = texture_data->height;

      Vector2f dim((float)width / image->x_count, (float)height / image->y_count);

      int count;
      sprite->frames = renderer.CreateSheet(texture_data, dim, &count);
      sprite->frame_count = count;
      sprite->duration = image->animation_time / 100.0f;

      anim->sprite = sprite;
      anim->t = 0.0f;
      anim->repeat = false;
    } else {
      fprintf(stderr, "Failed to get texture for lvz image.\n");
    }
  }

  ptr = data + sizeof(ObjectSectionHeader);

  for (u32 i = 0; i < header->object_count; ++i) {
    ObjectDefinition* def = (ObjectDefinition*)ptr;

    LvzObject* obj = objects + object_count++;
    obj->map_object = def->map_object;
    obj->object_id = def->object_id;
    obj->x_map = def->x_map;
    obj->y_map = def->y_map;
    obj->animation_index = animation_base + def->image_number;
    obj->layer = GetLayer(def->layer);
    obj->display_time = def->display_time;
    obj->display_mode = def->display_mode;

    DisplayMode display = (DisplayMode)obj->display_mode;
    if (display == DisplayMode::ShowAlways) {
      if (obj->map_object) {
        active_map_objects[active_map_object_count++] = obj;
      } else {
        active_screen_objects[active_screen_object_count++] = obj;
      }
    }

    ptr += sizeof(ObjectDefinition);
  }
}

void LoadShip(SpriteRenderer& renderer, const char* filename, u8* data, size_t size, int ship) {
  int width, height;

  u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

  if (image_data) {
    int count = 0;
    float dim = sqrt((width * height) / 40.0f);
    SpriteRenderable* renderables =
        renderer.LoadSheetFromMemory(filename, image_data, width, height, Vector2f(dim, dim), &count);

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
      float dim = sqrt((width * height) / 40.0f);
      Graphics::ship_sprites =
          renderer.LoadSheetFromMemory(filename, image_data, width, height, Vector2f(dim, dim), &count);
      ImageFree(image_data);
    }
  } else if (null_stricmp(extension_free, "ship1") == 0) {
    LoadShip(renderer, filename, data, size, 0);
  } else if (null_stricmp(extension_free, "ship2") == 0) {
    LoadShip(renderer, filename, data, size, 1);
  } else if (null_stricmp(extension_free, "ship3") == 0) {
    LoadShip(renderer, filename, data, size, 2);
  } else if (null_stricmp(extension_free, "ship4") == 0) {
    LoadShip(renderer, filename, data, size, 3);
  } else if (null_stricmp(extension_free, "ship5") == 0) {
    LoadShip(renderer, filename, data, size, 4);
  } else if (null_stricmp(extension_free, "ship6") == 0) {
    LoadShip(renderer, filename, data, size, 5);
  } else if (null_stricmp(extension_free, "ship7") == 0) {
    LoadShip(renderer, filename, data, size, 6);
  } else if (null_stricmp(extension_free, "ship8") == 0) {
    LoadShip(renderer, filename, data, size, 7);
  }
}

}  // namespace null
