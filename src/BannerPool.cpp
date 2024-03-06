#include "BannerPool.h"

#include <assert.h>
#include <glad/glad.h>
#include <stdio.h>
#include <string.h>

#include "Memory.h"
#include "PlayerManager.h"
#include "net/PacketDispatcher.h"
#include "render/Image.h"

namespace null {

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

  BannerRegistration* registration = AllocateRegistration(pid);

  if (!registration) return;

  WriteBanner(registration, banner_data);

  BannerTextureSheet* sheet = registration->sheet;
  size_t registration_index = registration->GetGlobalIndex(this->sheets);

  player_banners[pid] = registration_index;
}

void BannerPool::FreeBanner(u16 pid) {
  size_t registration_index = player_banners[pid];
  if (registration_index == -1) return;

  player_banners[pid] = -1;

  BannerRegistration* reg = registrations + registration_index;

  free_indexes[free_count++] = (u16)registration_index;
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
    u32 pixel = kDefaultColorTable[banner_data[i]];

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

BannerRegistration* BannerPool::AllocateRegistration(u16 pid) {
  BannerRegistration* registration = nullptr;

  size_t registration_index = player_banners[pid];

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
