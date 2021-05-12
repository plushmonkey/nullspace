#ifndef NULLSPACE_CHECKSUM_H_
#define NULLSPACE_CHECKSUM_H_

#include "../Types.h"

namespace null {

struct MemoryArena;

struct MemoryChecksumGenerator {
  MemoryChecksumGenerator() = delete;

  static bool Initialize(MemoryArena& arena, const char* text_section_filename, const char* data_section_filename);
  static u32 Generate(u32 key);

 private:
  static const char* text_section_;
  static const char* data_section_;
};

u8 crc8(const u8* ptr, size_t len);
u32 crc32(const u8* ptr, size_t size);

struct ArenaSettings;
u32 SettingsChecksum(u32 key, const ArenaSettings& settings);
u8 WeaponChecksum(const u8* data, size_t size);
u32 VieChecksum(u32 key);

}  // namespace null

#endif
