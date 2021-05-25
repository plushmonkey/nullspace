#include "BannerPool.h"

#include <assert.h>
#include <glad/glad.h>
#include <stdio.h>
#include <string.h>

#include "Memory.h"
#include "PlayerManager.h"
#include "net/PacketDispatcher.h"

namespace null {

static constexpr u32 kBannerColorTable[256] = {
    0x000000, 0x393939, 0x424242, 0x525252, 0x5a5a5a, 0x737373, 0x7b7b7b, 0x848484, 0x8c8c8c, 0x949494, 0x9c9c9c,
    0xa5a5a5, 0xadadad, 0xc6c6c6, 0xcecece, 0xbdb5b5, 0xb5adad, 0x9c9494, 0x7b7373, 0x6b6363, 0xffdede, 0xefc6c6,
    0x8c7373, 0xffcece, 0xefb5b5, 0x634a4a, 0xb58484, 0xffb5b5, 0xe78484, 0x7b4242, 0x211010, 0xff7b7b, 0x842929,
    0x521818, 0xce0808, 0xce2921, 0xf76352, 0xce5a4a, 0x94635a, 0xa54231, 0xde3108, 0xff3908, 0x731800, 0x4d2d04,
    0xa52900, 0x7b635a, 0xff8452, 0xce6339, 0xbd4208, 0x632100, 0xffb584, 0xef6b18, 0x733910, 0xde8439, 0x635a52,
    0x948c84, 0xce8c4a, 0x845a29, 0xffa542, 0xf78c18, 0xffd6a5, 0xad8c63, 0x845208, 0xad7310, 0xa56b08, 0xffe7bd,
    0xb57b10, 0x946308, 0xf7dead, 0x9c8c6b, 0xd6b573, 0xa58442, 0xf7b529, 0xefad21, 0x736b5a, 0x4a4231, 0xefce84,
    0xffce63, 0xdead42, 0xffbd29, 0xb59442, 0xefbd42, 0xffc631, 0x5a4208, 0xce9408, 0xffde84, 0xe7b521, 0xfff7de,
    0xefe7ce, 0xb5ad94, 0xb5a573, 0xffe79c, 0xffd65a, 0xa59c7b, 0x847b5a, 0xfff7d6, 0xffefad, 0x9c8418, 0xe7c629,
    0xad9c21, 0xffe721, 0xadada5, 0x9c9c8c, 0x4a4a42, 0xadad94, 0x313129, 0xa5a584, 0x5a5a18, 0xcece31, 0xffff39,
    0x7b8421, 0xd6e739, 0xa5b531, 0xadc639, 0x738439, 0x94b542, 0xb5ce7b, 0x739431, 0x526b29, 0xbdf763, 0x7ba542,
    0xa5de52, 0x7bb542, 0xe7f7de, 0x7b8c73, 0xa5bd9c, 0x9cf77b, 0xc6e7bd, 0x7bb56b, 0x529442, 0x398429, 0x183910,
    0x6bde5a, 0x295a21, 0x73ff63, 0x5ace4a, 0x429439, 0x6bce63, 0x4aad42, 0xadb5ad, 0xb5ceb5, 0x394239, 0x213121,
    0xa5f7a5, 0x397339, 0x215221, 0x296b29, 0x399439, 0x185218, 0x084a31, 0x104239, 0x313952, 0x31398c, 0x6b73ce,
    0x4a52b5, 0xefefff, 0xe7e7ff, 0xbdbdd6, 0x6b6b7b, 0xcecef7, 0xc6c6f7, 0x42425a, 0xa5a5e7, 0xb5b5ff, 0x4a4a6b,
    0x9494d6, 0x9c9ce7, 0x7373ad, 0x6b6ba5, 0xa5a5ff, 0x8484ce, 0x63639c, 0x7373b5, 0x525284, 0x7b7bc6, 0x42426b,
    0x5a5a94, 0x4a4a7b, 0x6363a5, 0x393963, 0x29294a, 0x42427b, 0x313163, 0x393973, 0x6363ce, 0x6363ff, 0x5252d6,
    0x4242bd, 0x212163, 0x5252ff, 0x3939b5, 0x4242de, 0x212173, 0x31297b, 0x635a94, 0x8c84ad, 0x635a8c, 0x4a29a5,
    0x4210c6, 0x290094, 0x4a2994, 0xb59ce7, 0x211831, 0x63429c, 0x3f2d0e, 0x210052, 0x31007b, 0x8463ad, 0x8442d6,
    0x6321b5, 0x390084, 0xb584ef, 0xad6bf7, 0x7342ad, 0x844ac6, 0x52297b, 0x4a2173, 0x310063, 0x4a0094, 0x5200a5,
    0x4a395a, 0x634a7b, 0xce94ff, 0x9c6bc6, 0x7b42ad, 0x9c52de, 0x4a0884, 0x210039, 0xad63c6, 0x4a0063, 0x9c63a5,
    0x630073, 0x7b737b, 0x0a190a, 0x4a004a, 0x520052, 0x7b0073, 0x7b4a73, 0x4a3142, 0x632942, 0x846b73, 0x94737b,
    0xff7394, 0x633942, 0x946b73, 0x9c394a, 0xff8c9c, 0xbd6b73, 0xef6373, 0xce2939, 0xff394a, 0x7b1821, 0xf7737b,
    0xd63942, 0xa52129, 0xffffff,
};

void OnBannerPkt(void* user, u8* pkt, size_t size) {
  BannerPool* pool = (BannerPool*)user;

  pool->OnBanner(pkt, size);
}

BannerPool::BannerPool(MemoryArena& temp_arena, PlayerManager& player_manager, PacketDispatcher& dispatcher)
    : temp_arena(temp_arena), player_manager(player_manager) {
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(registrations); ++i) {
    registrations[i].index = 0xFFFFFFFF;
    registrations[i].sheet = nullptr;
    registrations[i].player_id = 0xFFFF;
  }

  memset(player_banners, 0xFF, sizeof(player_banners));

  dispatcher.Register(ProtocolS2C::PlayerBannerChange, OnBannerPkt, this);
}

void BannerPool::OnBanner(u8* pkt, size_t size) {
  u16 pid = *(u16*)(pkt + 1);
  u8* banner_data = pkt + 3;

  u16 player_index = player_manager.GetPlayerIndex(pid);
  if (player_index == kInvalidPlayerId) return;

  BannerRegistration* registration = GetRegistration(pid, player_index);

  if (!registration) return;

  WriteBanner(registration, banner_data);

  BannerTextureSheet* sheet = registration->sheet;
  size_t registration_index = registration->GetGlobalIndex(this->sheets);

  player_banners[player_index] = registration_index;
}

void BannerPool::FreeBanner(u16 pid) {
  u16 player_index = player_manager.GetPlayerIndex(pid);

  assert(player_index < NULLSPACE_ARRAY_SIZE(player_banners));
  if (player_index >= NULLSPACE_ARRAY_SIZE(player_banners)) return;

  size_t registration_index = player_banners[player_index];
  if (registration_index == -1) return;

  player_banners[player_index] = -1;

  BannerRegistration* reg = registrations + registration_index;

  free_indexes[free_count++] = (u16)reg->GetGlobalIndex(this->sheets);
}

void BannerPool::WriteBanner(BannerRegistration* registration, u8* banner_data) {
  BannerTextureSheet* sheet = registration->sheet;

  GLint y_start = (GLint)((registration->index * kBannerWidth) / kBannerSheetWidth);
  GLint x_start = (GLint)((registration->index * kBannerWidth) % kBannerSheetWidth);
  GLint y_end = (GLint)(y_start + kBannerHeight);
  GLint x_end = (GLint)(x_start + kBannerWidth);

  float x_start_uv = x_start / (float)kBannerSheetWidth;
  float x_end_uv = x_end / (float)kBannerSheetWidth;
  float y_start_uv = y_start / (float)kBannerSheetHeight;
  float y_end_uv = y_end / (float)kBannerSheetHeight;

  registration->renderable.texture = sheet->texture;
  registration->renderable.dimensions = Vector2f(12, 8);
  registration->renderable.uvs[0] = Vector2f(x_start_uv, y_start_uv);
  registration->renderable.uvs[1] = Vector2f(x_end_uv, y_start_uv);
  registration->renderable.uvs[2] = Vector2f(x_start_uv, y_end_uv);
  registration->renderable.uvs[3] = Vector2f(x_end_uv, y_end_uv);

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();
  u8* expanded_data = temp_arena.Allocate(kBannerWidth * 3);

  for (size_t i = 0; i < 96; ++i) {
    u32 pixel = kBannerColorTable[banner_data[i]];

    expanded_data[i * 3 + 0] = (pixel >> 16) & 0xFF;
    expanded_data[i * 3 + 1] = (pixel >> 8) & 0xFF;
    expanded_data[i * 3 + 2] = pixel & 0xFF;
  }

  // TODO: This should be done elsewhere to abstract renderer
  // Write new data into texture

  glBindTexture(GL_TEXTURE_2D, sheet->texture);

  glTexSubImage2D(GL_TEXTURE_2D, 0, x_start, y_start, kBannerWidth, kBannerHeight, GL_RGB, GL_UNSIGNED_BYTE,
                  expanded_data);

  temp_arena.Revert(snapshot);
}

BannerRegistration* BannerPool::GetRegistration(u16 pid, size_t player_index) {
  BannerRegistration* registration = nullptr;

  size_t registration_index = player_banners[player_index];

  if (registration_index != -1) {
    registration = registrations + registration_index;
  }

  if (!registration) {
    if (free_count > 0) {
      u16 registration_index = free_indexes[--free_count];

      registration = registrations + registration_index;
    } else {
      assert(registration_count < NULLSPACE_ARRAY_SIZE(registrations));

      size_t index = registration_count++;
      size_t sheet_index = index / kBannersPerSheet;

      assert(sheet_index < kBannerSheetCount);

      BannerTextureSheet* sheet = sheets + sheet_index;

      registration = registrations + index;

      registration->index = index - sheet_index * kBannersPerSheet;
      registration->sheet = sheet;

      if (sheet->texture == 0xFFFFFFFF) {
        // TODO: This should be done elsewhere to abstract renderer
        CreateTexture(&sheet->texture);
      }

      ++registration->sheet->count;
    }

    registration->player_id = pid;
  }

  return registration;
}

void BannerPool::CreateTexture(u32* texture_index) {
  glGenTextures(1, texture_index);
  assert(*texture_index != -1);

  glBindTexture(GL_TEXTURE_2D, *texture_index);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kBannerSheetWidth, kBannerSheetHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
}

void BannerPool::Cleanup() {
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(sheets); ++i) {
    if (sheets[i].texture != -1) {
      glDeleteTextures(1, &sheets[i].texture);
      sheets[i].texture = -1;
    }
  }

  free_count = 0;
  registration_count = 0;
  memset(player_banners, 0xFF, sizeof(player_banners));
}

}  // namespace null
