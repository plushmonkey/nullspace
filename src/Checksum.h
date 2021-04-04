#ifndef NULLSPACE_CHECKSUM_H_
#define NULLSPACE_CHECKSUM_H_

#include "Types.h"

namespace null {

struct MemoryChecksumGenerator {
  MemoryChecksumGenerator() = delete;

  static void Initialize(const char* text_section, const char* data_section);
  static u32 Generate(u32 key);

 private:
  static const char* text_section_;
  static const char* data_section_;
};

u8 crc8(const u8* ptr, size_t len);
u32 crc32(const u8* ptr, size_t size);

}  // namespace null

#endif
