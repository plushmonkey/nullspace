#include "LvzController.h"

#include <cassert>
#include <cstdio>

#include "Buffer.h"
#include "Clock.h"
#include "FileRequester.h"
#include "Inflate.h"
#include "Logger.h"
#include "Memory.h"
#include "Platform.h"
#include "Player.h"
#include "SpectateView.h"
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

enum OverwriteFlag {
  Overwrite_Bullets,
  Overwrite_BulletTrails,
  Overwrite_Ships,
  Overwrite_Ship1,
  Overwrite_Ship2,
  Overwrite_Ship3,
  Overwrite_Ship4,
  Overwrite_Ship5,
  Overwrite_Ship6,
  Overwrite_Ship7,
  Overwrite_Ship8,
  Overwrite_Bombs,
  Overwrite_Mines,
  Overwrite_BombTrails,
  Overwrite_Repel,
  Overwrite_Explode2,
  Overwrite_EmpBurst,
  Overwrite_Flag,
  Overwrite_Prizes,
  Overwrite_Exhaust,
  Overwrite_Rocket,
  Overwrite_Portal,
  Overwrite_EmpSpark,
};

struct ObjectImageList {
  size_t count;
  char filenames[256][256];

  bool Contains(const char* filename) {
    for (size_t i = 0; i < count; ++i) {
      if (strcmp(filename, filenames[i]) == 0) {
        return true;
      }
    }

    return false;
  }
};

enum class DisplayMode { ShowAlways, EnterZone, EnterArena, Kill, Death, ServerControlled };

enum class ReferencePoint {
  Normal,
  ScreenCenter,
  BottomRight,
  StatBoxBottomRight,
  SpecialTopRight,
  SpecialBottomRight,
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

Vector2f GetScreenReferencePoint(Player& self, SpectateView& specview, Camera& ui_camera, ReferencePoint point) {
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
    case ReferencePoint::EnergyBelow: {
      float x = ui_camera.surface_dim.x * 0.5f;
      float y = 0.0f;

      if (self.ship == 8) {
        y = specview.render_extra_lines * 12.0f;
      } else {
        y = Graphics::healthbar_sprites[0].dimensions.y;
      }

      return Vector2f(x, y);
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

static void OnLvzModifyPkt(void* user, u8* pkt, size_t size) {
  LvzController* controller = (LvzController*)user;

  controller->OnLvzModify(pkt, size);
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
  dispatcher.Register(ProtocolS2C::ModifyLVZ, OnLvzModifyPkt, this);
}

void LvzController::Update(float dt) {
  for (size_t i = 0; i < animation_count; ++i) {
    Animation* anim = animations + i;

    anim->t += dt;
  }

  u32 tick = GetCurrentTick();

  // Disable any timed out screen objects
  for (size_t i = 0; i < active_screen_object_count; ++i) {
    LvzObject* object = active_screen_objects[i];

    if (object->display_time > 0 && TICK_DIFF(tick, object->enabled_tick) >= object->display_time * 10) {
      DisableObject(object->object_id);
    }
  }

  // Disable any timed out map objects
  for (size_t i = 0; i < active_map_object_count; ++i) {
    LvzObject* object = active_map_objects[i];

    if (object->display_time > 0 && TICK_DIFF(tick, object->enabled_tick) >= object->display_time * 10) {
      DisableObject(object->object_id);
    }
  }
}

void LvzController::Render(Camera& ui_camera, Camera& game_camera, Player* self, SpectateView& specview) {
  if (!self) return;

  for (size_t i = 0; i < active_screen_object_count; ++i) {
    LvzObject* obj = active_screen_objects[i];

    ReferencePoint reference_x = (ReferencePoint)obj->x_type;
    ReferencePoint reference_y = (ReferencePoint)obj->y_type;

    Vector2f base_x = GetScreenReferencePoint(*self, specview, ui_camera, reference_x);
    Vector2f base_y = GetScreenReferencePoint(*self, specview, ui_camera, reference_y);

    Vector2f position(base_x.x + (float)obj->x_screen, base_y.y + (float)obj->y_screen);
    Animation* anim = animations + obj->animation_index;

    if (anim->sprite->frames == nullptr) continue;

    renderer.Draw(ui_camera, anim->GetFrame(), position, obj->layer);
  }

  renderer.Render(ui_camera);

  for (size_t i = 0; i < active_map_object_count; ++i) {
    LvzObject* obj = active_map_objects[i];

    Vector2f position(obj->x_map / 16.0f, obj->y_map / 16.0f);
    Animation* anim = animations + obj->animation_index;

    if (anim->sprite->frames == nullptr) continue;

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
      DisableObject(toggle->id);
    } else {
      for (size_t j = 0; j < object_count; ++j) {
        LvzObject* obj = objects + j;

        if (obj->object_id == toggle->id) {
          // Disable any previous ones with this id before adding it in again
          DisableObject(toggle->id);

          animations[obj->animation_index].t = 0.0f;
          obj->enabled_tick = GetCurrentTick();

          if (obj->map_object) {
            active_map_objects[active_map_object_count++] = obj;
            assert(active_map_object_count < NULLSPACE_ARRAY_SIZE(active_map_objects));
          } else {
            active_screen_objects[active_screen_object_count++] = obj;
            assert(active_screen_object_count < NULLSPACE_ARRAY_SIZE(active_screen_objects));
          }

          break;
        }
      }
    }
  }
}

void LvzController::DisableObject(u16 id) {
  for (size_t j = 0; j < active_map_object_count; ++j) {
    if (active_map_objects[j]->object_id == id) {
      active_map_objects[j--] = active_map_objects[--active_map_object_count];
    }
  }

  for (size_t j = 0; j < active_screen_object_count; ++j) {
    if (active_screen_objects[j]->object_id == id) {
      active_screen_objects[j--] = active_screen_objects[--active_screen_object_count];
    }
  }
}

void LvzController::OnLvzModify(u8* pkt, size_t size) {
#pragma pack(push, 1)
  struct ModifyBitfield {
    u8 xy : 1;
    u8 image : 1;
    u8 layer : 1;
    u8 time : 1;
    u8 mode : 1;
    u8 reserved : 3;
  };
#pragma pack(pop)

  ModifyBitfield* mod = (ModifyBitfield*)(pkt + 1);

#if 0
  printf("Lvz modify (%zd): %d %d %d %d %d %d\n", size, mod->xy, mod->image, mod->layer, mod->time, mod->mode,
         mod->reserved);
#endif
}

void LvzController::OnMapInformation(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // 0x29

  // Skip map file
  buffer.ReadString(20);

  // Skip over file size if using Continuum encryption only
  if (buffer.write - buffer.read > 0) {
    buffer.ReadString(4);
  }

  u16 index = 1;
  while (buffer.read < buffer.write) {
    char* raw_filename = buffer.ReadString(16);
    u32 checksum = buffer.ReadU32();
    u32 filesize = buffer.ReadU32();

    char filename[17];
    memcpy(filename, raw_filename, 16);
    filename[16] = 0;

    requester.Request(filename, index++, filesize, checksum, false, OnDownload, this);
  }
}

void LvzController::OnFileDownload(struct FileRequest* request, u8* data) {
  LvzHeader* lvz_header = (LvzHeader*)data;

  if (lvz_header->magic != kMagicValue) {
    Log(LogLevel::Warning, "Received lvz file %s that didn't contain lvz format.", request->filename);
    return;
  }

  u8* ptr = data + sizeof(LvzHeader);

  ObjectImageList* object_images = memory_arena_push_type(&temp_arena, ObjectImageList);
  object_images->count = 0;

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
        Log(LogLevel::Warning, "Failed to uncompress lvz data.");
        ptr += section_header->compressed_size;
        continue;
      }
      size = decompressed_size;
    }

    ptr += section_header->compressed_size;

    if (section_header->timestamp != 0 || *filename != 0) {
      int width, height;

      u8* image_data = ImageLoadFromMemory(section_data, size, &width, &height);
      if (image_data) {
        GLuint texture_id = renderer.CreateTexture(filename, image_data, width, height);

        ImageFree(image_data);
      }
    } else {
      ProcessObjects(object_images, section_data, size);
    }

    temp_arena.Revert(snapshot);
  }

  // Loop again after processing the objects to do graphic replacements.
  // This is done afterwards because the object images can't be used to replace default graphics.
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
        Log(LogLevel::Warning, "Failed to uncompress lvz data.");
        ptr += section_header->compressed_size;
        continue;
      }
      size = decompressed_size;
    }

    ptr += section_header->compressed_size;

    if (section_header->timestamp != 0 || *filename != 0) {
      if (!object_images->Contains(filename)) {
        ProcessGraphicFile(filename, section_data, size);
      }
    }

    temp_arena.Revert(snapshot);
  }

  ProcessMissing();
}

void LvzController::ProcessMissing() {
  for (size_t i = 0; i < pending_animation_count; ++i) {
    PendingAnimation* pending = pending_animations + i;

    TextureData* texture_data = renderer.texture_map->Find(pending->filename);

    if (texture_data) {
      Animation* anim = pending->animation;

      int width = texture_data->width;
      int height = texture_data->height;

      Vector2f dim((float)width / pending->x_count, (float)height / pending->y_count);

      int count;
      anim->sprite->frames = renderer.CreateSheet(texture_data, dim, &count);
      anim->sprite->frame_count = count;

      anim->t = 0.0f;
      anim->repeat = false;

      pending_animations[i--] = pending_animations[--pending_animation_count];
    }
  }
}

void LvzController::ProcessObjects(struct ObjectImageList* object_images, u8* data, size_t size) {
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

    strcpy(object_images->filenames[object_images->count++], filename);

    TextureData* texture_data = renderer.texture_map->Find(filename);

    if (texture_data) {
      AnimatedSprite* sprite = sprites + animation_count;
      Animation* anim = animations + animation_count++;

      assert(animation_count < NULLSPACE_ARRAY_SIZE(animations));

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
      // Stick empty animation in if file wasn't found.
      AnimatedSprite* sprite = sprites + animation_count;
      Animation* anim = animations + animation_count++;

      assert(animation_count < NULLSPACE_ARRAY_SIZE(animations));

      sprite->frames = nullptr;
      sprite->frame_count = 0;
      sprite->duration = image->animation_time / 100.0f;

      anim->sprite = nullptr;
      anim->t = 0.0f;
      anim->repeat = false;
      anim->sprite = sprite;

      PendingAnimation* pending = pending_animations + pending_animation_count++;

      assert(pending_animation_count < NULLSPACE_ARRAY_SIZE(pending_animations));

      strcpy(pending->filename, filename);
      pending->x_count = image->x_count;
      pending->y_count = image->y_count;
      pending->animation = anim;
    }
  }

  ptr = data + sizeof(ObjectSectionHeader);

  for (u32 i = 0; i < header->object_count; ++i) {
    ObjectDefinition* def = (ObjectDefinition*)ptr;

    LvzObject* obj = objects + object_count++;

    assert(object_count < NULLSPACE_ARRAY_SIZE(objects));

    obj->map_object = def->map_object;
    obj->object_id = def->object_id;
    obj->x_map = def->x_map;
    obj->y_map = def->y_map;
    obj->animation_index = animation_base + def->image_number;
    obj->layer = GetLayer(def->layer);
    obj->display_time = def->display_time;
    obj->display_mode = def->display_mode;
    obj->enabled_tick = GetCurrentTick();

    DisplayMode display = (DisplayMode)obj->display_mode;
    if (display == DisplayMode::ShowAlways) {
      if (obj->map_object) {
        active_map_objects[active_map_object_count++] = obj;
        assert(active_map_object_count < NULLSPACE_ARRAY_SIZE(active_map_objects));
      } else {
        active_screen_objects[active_screen_object_count++] = obj;
        assert(active_screen_object_count < NULLSPACE_ARRAY_SIZE(active_screen_objects));
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
void LvzController::ProcessGraphicFile(const char* filename, u8* data, size_t size) {
  const char* extension = strstr(filename, ".");
  char extension_free[256];

  if (extension == nullptr) return;

  size_t name_size = extension - filename;

  memcpy(extension_free, filename, name_size);
  extension_free[name_size] = 0;

  if ((overwrite & (1 << Overwrite_Ships)) == 0 && null_stricmp(extension_free, "ships") == 0) {
    overwrite |= (1 << Overwrite_Ships);
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
  } else if ((overwrite & (1 << Overwrite_Ship1)) == 0 && null_stricmp(extension_free, "ship1") == 0) {
    overwrite |= (1 << Overwrite_Ship1);
    LoadShip(renderer, filename, data, size, 0);
  } else if ((overwrite & (1 << Overwrite_Ship2)) == 0 && null_stricmp(extension_free, "ship2") == 0) {
    overwrite |= (1 << Overwrite_Ship2);
    LoadShip(renderer, filename, data, size, 1);
  } else if ((overwrite & (1 << Overwrite_Ship3)) == 0 && null_stricmp(extension_free, "ship3") == 0) {
    overwrite |= (1 << Overwrite_Ship3);
    LoadShip(renderer, filename, data, size, 2);
  } else if ((overwrite & (1 << Overwrite_Ship4)) == 0 && null_stricmp(extension_free, "ship4") == 0) {
    overwrite |= (1 << Overwrite_Ship4);
    LoadShip(renderer, filename, data, size, 3);
  } else if ((overwrite & (1 << Overwrite_Ship5)) == 0 && null_stricmp(extension_free, "ship5") == 0) {
    overwrite |= (1 << Overwrite_Ship5);
    LoadShip(renderer, filename, data, size, 4);
  } else if ((overwrite & (1 << Overwrite_Ship6)) == 0 && null_stricmp(extension_free, "ship6") == 0) {
    overwrite |= (1 << Overwrite_Ship6);
    LoadShip(renderer, filename, data, size, 5);
  } else if ((overwrite & (1 << Overwrite_Ship7)) == 0 && null_stricmp(extension_free, "ship7") == 0) {
    overwrite |= (1 << Overwrite_Ship7);
    LoadShip(renderer, filename, data, size, 6);
  } else if ((overwrite & (1 << Overwrite_Ship8)) == 0 && null_stricmp(extension_free, "ship8") == 0) {
    overwrite |= (1 << Overwrite_Ship8);
    LoadShip(renderer, filename, data, size, 7);
  } else if ((overwrite & (1 << Overwrite_Bullets)) == 0 && null_stricmp(extension_free, "bullets") == 0) {
    overwrite |= (1 << Overwrite_Bullets);
    renderer.FreeSheet(Graphics::bullet_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(width / 4.0f, width / 4.0f);
      Graphics::bullet_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateBulletAnimations(Graphics::bullet_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_BulletTrails)) == 0 && null_stricmp(extension_free, "gradient") == 0) {
    overwrite |= (1 << Overwrite_BulletTrails);
    renderer.FreeSheet(Graphics::bullet_trail_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Graphics::bullet_trail_sprites =
          renderer.LoadSheetFromMemory(filename, image_data, width, height, Vector2f(1, 1), &count);
      Graphics::CreateBulletTrailAnimations(Graphics::bullet_trail_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Bombs)) == 0 && null_stricmp(extension_free, "bombs") == 0) {
    overwrite |= (1 << Overwrite_Bombs);
    renderer.FreeSheet(Graphics::bomb_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(width / 10.0f, width / 10.0f);
      Graphics::bomb_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateBombAnimations(Graphics::bomb_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Mines)) == 0 && null_stricmp(extension_free, "mines") == 0) {
    overwrite |= (1 << Overwrite_Mines);
    renderer.FreeSheet(Graphics::mine_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(width / 10.0f, width / 10.0f);
      Graphics::mine_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateMineAnimations(Graphics::mine_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_BombTrails)) == 0 && null_stricmp(extension_free, "trail") == 0) {
    overwrite |= (1 << Overwrite_BombTrails);
    renderer.FreeSheet(Graphics::bomb_trail_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(width / 10.0f, width / 10.0f);
      Graphics::bomb_trail_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateBombTrailAnimations(Graphics::bomb_trail_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Repel)) == 0 && null_stricmp(extension_free, "repel") == 0) {
    overwrite |= (1 << Overwrite_Repel);
    renderer.FreeSheet(Graphics::repel_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim_x = sqrt((width * height) / 10.0f);
      Vector2f dim(dim_x, dim_x);
      Graphics::repel_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateRepelAnimations(Graphics::repel_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Explode2)) == 0 && null_stricmp(extension_free, "explode2") == 0) {
    overwrite |= (1 << Overwrite_Explode2);
    renderer.FreeSheet(Graphics::explode2_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim_x = sqrt((width * height) / 44.0f);
      Vector2f dim(dim_x, dim_x);
      Graphics::explode2_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateBombExplodeAnimations(Graphics::explode2_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_EmpBurst)) == 0 && null_stricmp(extension_free, "empburst") == 0) {
    overwrite |= (1 << Overwrite_EmpBurst);
    renderer.FreeSheet(Graphics::emp_burst_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim_x = sqrt((width * height) / 10.0f);
      Vector2f dim(dim_x, dim_x);
      Graphics::emp_burst_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateEmpExplodeAnimations(Graphics::emp_burst_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Flag)) == 0 && null_stricmp(extension_free, "flag") == 0) {
    overwrite |= (1 << Overwrite_Flag);
    renderer.FreeSheet(Graphics::flag_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim_x = width / 10.0f;
      Vector2f dim(dim_x, dim_x);
      Graphics::flag_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateFlagAnimations(Graphics::flag_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Prizes)) == 0 && null_stricmp(extension_free, "prizes") == 0) {
    overwrite |= (1 << Overwrite_Prizes);
    renderer.FreeSheet(Graphics::prize_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      float dim_x = width / 10.0f;
      Vector2f dim(dim_x, dim_x);
      Graphics::prize_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreatePrizeAnimations(Graphics::prize_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Exhaust)) == 0 && null_stricmp(extension_free, "exhaust") == 0) {
    overwrite |= (1 << Overwrite_Exhaust);
    renderer.FreeSheet(Graphics::exhaust_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(16.0f, 16.0f);
      Graphics::exhaust_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateExhaustAnimations(Graphics::exhaust_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Rocket)) == 0 && null_stricmp(extension_free, "rocket") == 0) {
    overwrite |= (1 << Overwrite_Rocket);
    renderer.FreeSheet(Graphics::rocket_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(24.0f, 24.0f);
      Graphics::rocket_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreateRocketAnimations(Graphics::rocket_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_Portal)) == 0 && null_stricmp(extension_free, "warppnt") == 0) {
    overwrite |= (1 << Overwrite_Portal);
    renderer.FreeSheet(Graphics::portal_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(16.0f, 16.0f);
      Graphics::portal_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreatePortalAnimations(Graphics::portal_sprites, count);
      ImageFree(image_data);
    }
  } else if ((overwrite & (1 << Overwrite_EmpSpark)) == 0 && null_stricmp(extension_free, "spark") == 0) {
    overwrite |= (1 << Overwrite_EmpSpark);
    renderer.FreeSheet(Graphics::emp_spark_sprites[0].texture);

    int width, height;

    u8* image_data = ImageLoadFromMemory(data, size, &width, &height);

    if (image_data) {
      int count = 0;
      Vector2f dim(40.0f, 40.0f);
      Graphics::emp_spark_sprites = renderer.LoadSheetFromMemory(filename, image_data, width, height, dim, &count);
      Graphics::CreatePortalAnimations(Graphics::emp_spark_sprites, count);
      ImageFree(image_data);
    }
  }
}

void LvzController::Reset() {
  overwrite = 0;
  pending_animation_count = 0;
  active_map_object_count = 0;
  active_screen_object_count = 0;
  object_count = 0;
  animation_count = 0;
}

}  // namespace null
