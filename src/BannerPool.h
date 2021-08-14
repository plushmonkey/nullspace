#ifndef NULLSPACE_BANNERMANAGER_H_
#define NULLSPACE_BANNERMANAGER_H_

#include "Types.h"
#include "render/Sprite.h"

namespace null {

struct MemoryArena;
struct PacketDispatcher;
struct PlayerManager;

// This should be tuned to minimize the spread across sheets without wasting too much graphics memory
constexpr size_t kBannerSheetCount = 16;
constexpr size_t kBannersPerSheet = 1024 / kBannerSheetCount;

constexpr size_t kBannerWidth = 12;
constexpr size_t kBannerHeight = 8;
constexpr size_t kBannerSheetWidth = kBannersPerSheet * kBannerWidth;
constexpr size_t kBannerSheetHeight = kBannersPerSheet * kBannerHeight;

struct BannerTextureSheet {
  size_t count = 0;
  u32 texture = 0xFFFFFFFF;
};

struct BannerRegistration {
  BannerTextureSheet* sheet;

  // Index into the sheet
  size_t index;
  SpriteRenderable renderable;

  u16 player_id;

  inline size_t GetGlobalIndex(BannerTextureSheet* sheets) { return (sheet - sheets) * kBannersPerSheet + index; }
};

struct BannerPool {
  MemoryArena& temp_arena;
  PlayerManager& player_manager;

  BannerTextureSheet sheets[kBannerSheetCount];

  size_t registration_count = 0;
  BannerRegistration registrations[1024];

  // Index into banner registrations for player id
  size_t player_banners[65535];

  size_t free_count = 0;
  u16 free_indexes[1024];

  BannerPool(MemoryArena& temp_arena, PlayerManager& player_manager, PacketDispatcher& dispatcher);
  void Cleanup();

  void OnBanner(u8* pkt, size_t size);

  void FreeBanner(u16 pid);

  inline BannerRegistration* GetRegistration(u16 pid) {
    size_t registration_index = player_banners[pid];

    return registration_index == -1 ? nullptr : registrations + registration_index;
  }

 private:
  void CreateTexture(u32* texture_index);
  BannerRegistration* AllocateRegistration(u16 pid);
  void WriteBanner(BannerRegistration* registration, u8* banner_data);
};

}  // namespace null

#endif
