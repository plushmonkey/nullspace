#include "Checksum.h"

#include <cassert>
#include <cstdio>

#include "../ArenaSettings.h"
#include "../Memory.h"
#include "MD5.h"

namespace null {

const char* MemoryChecksumGenerator::text_section_ = nullptr;
const char* MemoryChecksumGenerator::data_section_ = nullptr;

char* LoadFile(MemoryArena& arena, const char* path) {
#pragma warning(push)
#pragma warning(disable : 4996)
  FILE* f = fopen(path, "rb");
#pragma warning(pop)

  if (!f) {
    return nullptr;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* data = (char*)arena.Allocate(size);

  fread(data, 1, size, f);
  fclose(f);

  return data;
}

bool MemoryChecksumGenerator::Initialize(MemoryArena& arena, const char* text_section_filename,
                                         const char* data_section_filename) {
  char* mem_text = LoadFile(arena, "cont_mem_text");
  char* mem_data = LoadFile(arena, "cont_mem_data");

  if (!mem_text || !mem_data) {
    fprintf(stderr, "Requires Continuum dumped memory files cont_mem_text and cont_mem_data\n");
    return false;
  }

  text_section_ = mem_text;
  data_section_ = mem_data;

  return true;
}

u32 MemoryChecksumGenerator::Generate(u32 key) {
  constexpr u32 kChecksumMangler = 7193;

  MD5_CTX ctx;
  MD5Init(&ctx, kChecksumMangler);

  key = key * 0x10dcd + 0x4271;

  u32 result = key;
  u32 offset = 0x401000;
  const char* text = text_section_;
  const char* data = data_section_;

  assert(text);
  assert(data);

  do {
    if ((offset & 0x7FF) == 0) {
      if ((offset & 0x3FFF) == 0) {
        if (offset == 0x408000) {
          const u32 table[16] = {
              0x5e15b28d, 0x4d2b9852, 0x1c23b9ef, 0x0d3d6503, 0xccb0edce, 0xded2a666, 0x3f187861, 0x1f2f7c89,
              0x4d8b2ced, 0x3a482c78, 0xa7d65dae, 0xeb036aed, 0x016b4fee, 0x9e02729e, 0x74dbcf80, 0x6f7ea6d8,
          };

          for (int i = 0; i < 0x40; ++i) {
            result ^= ((u8*)table)[i] * (i + 0x25);
          }
        }

        // Do two rounds of MD5 update
        const unsigned char* ptr = (const unsigned char*)text;
        for (int i = 0; i < 2; ++i) {
          MD5Update(&ctx, ptr, 64);
          ptr += 64;
        }
      }

      key = key * 0x10dcd + 0x4271;
      result = result + 0x34d927 ^ (result - 0x53933A9) ^ ctx.buf[1] ^ ctx.buf[3] ^ ctx.buf[2] ^ offset ^ result ^ key;
    }

    result =
        result ^ (*(u32*)(text + 0x04) + (ctx.buf[0] - *(u32*)(text)) ^ *(u32*)(text + 0x08)) - *(u32*)(text + 0x0C);
    offset += 0x10;
    text += 0x10;
  } while (offset < 0x4A7000);

  offset = 0x4A743C;

  // Move data to the start of the memory checksum area to make memory dumping easier
  data += 0x43C;

  do {
    if ((offset & 0x7FF) == 0) {
      if ((offset & 0x3FFF) == 0) {
        // Do two rounds of MD5 update
        const unsigned char* ptr = (const unsigned char*)data;
        for (int i = 0; i < 2; ++i) {
          MD5Update(&ctx, ptr, 64);
          ptr += 64;
        }
      }

      key = key * 0x10dcd + 0x4271;
      result = result + 0x34d927 ^ (result - 0x53933A9) ^ ctx.buf[1] ^ ctx.buf[3] ^ ctx.buf[2] ^ offset ^ result ^ key;
    }

    result = result ^ (*(u32*)(data + 0x04) + *(u32*)(data + 0x08)) - *(u32*)(data + 0x0C) ^ ctx.buf[0] - *(u32*)(data);

    offset += 0x10;
    data += 0x10;
  } while (offset < 0x4B3E90);

  return result;
}

static u8 crc8_table[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41, 0x9d, 0xc3, 0x21,
    0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc, 0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c,
    0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62, 0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c,
    0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff, 0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66,
    0xe5, 0xbb, 0x59, 0x07, 0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4,
    0x9a, 0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24, 0xf8, 0xa6,
    0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9, 0x8c, 0xd2, 0x30, 0x6e, 0xed,
    0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd, 0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92,
    0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50, 0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1,
    0x8f, 0x0c, 0x52, 0xb0, 0xee, 0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf,
    0x2d, 0x73, 0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b, 0x57,
    0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16, 0xe9, 0xb7, 0x55, 0x0b,
    0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8, 0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9,
    0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35};

u8 crc8(const u8* ptr, size_t len) {
  u8 crc = 0;

  while (len--) {
    crc = crc8_table[*ptr++ ^ crc];
  }

  return crc;
}

uint32_t crc32_for_byte(uint32_t r) {
  for (int j = 0; j < 8; ++j) {
    r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
  }

  return r ^ (uint32_t)0xFF000000L;
}

u32 crc32(const u8* ptr, size_t size) {
  static uint32_t table[0x100];
  u32 crc = 0;

  if (!*table) {
    for (size_t i = 0; i < 0x100; ++i) {
      table[i] = crc32_for_byte(static_cast<uint32_t>(i));
    }
  }

  for (size_t i = 0; i < size; ++i) {
    crc = table[(uint8_t)crc ^ ((uint8_t*)ptr)[i]] ^ crc >> 8;
  }

  return crc;
}

u32 SettingsChecksum(u32 key, const ArenaSettings& settings) {
  u32* data = (u32*)&settings;
  u32 sum = 0;

  for (int i = 0; i < sizeof(settings) / sizeof(u32); ++i, ++data) {
    sum += (*data ^ key);
  }

  return sum;
}

u8 WeaponChecksum(const u8* data, size_t size) {
  u8 checksum = 0;

  for (size_t i = 0; i < size; ++i) {
    checksum ^= data[i];
  }

  return checksum;
}

u32 VieChecksum(u32 key) {
  u32 part, csum = 0;

  part = 0xc98ed41f;
  part += 0x3e1bc | key;
  part ^= 0x42435942 ^ key;
  part += 0x1d895300 | key;
  part ^= 0x6b5c4032 ^ key;
  part += 0x467e44 | key;
  part ^= 0x516c7eda ^ key;
  part += 0x8b0c708b | key;
  part ^= 0x6b3e3429 ^ key;
  part += 0x560674c9 | key;
  part ^= 0xf4e6b721 ^ key;
  part += 0xe90cc483 | key;
  part ^= 0x80ece15a ^ key;
  part += 0x728bce33 | key;
  part ^= 0x1fc5d1e6 ^ key;
  part += 0x8b0c518b | key;
  part ^= 0x24f1a96e ^ key;
  part += 0x30ae0c1 | key;
  part ^= 0x8858741b ^ key;
  csum += part;

  part = 0x9c15857d;
  part += 0x424448b | key;
  part ^= 0xcd0455ee ^ key;
  part += 0x727 | key;
  part ^= 0x8d7f29cd ^ key;
  csum += part;

  part = 0x824b9278;
  part += 0x6590 | key;
  part ^= 0x8e16169a ^ key;
  part += 0x8b524914 | key;
  part ^= 0x82dce03a ^ key;
  part += 0xfa83d733 | key;
  part ^= 0xb0955349 ^ key;
  part += 0xe8000003 | key;
  part ^= 0x7cfe3604 ^ key;
  csum += part;

  part = 0xe3f8d2af;
  part += 0x2de85024 | key;
  part ^= 0xbed0296b ^ key;
  part += 0x587501f8 | key;
  part ^= 0xada70f65 ^ key;
  csum += part;

  part = 0xcb54d8a0;
  part += 0xf000001 | key;
  part ^= 0x330f19ff ^ key;
  part += 0x909090c3 | key;
  part ^= 0xd20f9f9f ^ key;
  part += 0x53004add | key;
  part ^= 0x5d81256b ^ key;
  part += 0x8b004b65 | key;
  part ^= 0xa5312749 ^ key;
  part += 0xb8004b67 | key;
  part ^= 0x8adf8fb1 ^ key;
  part += 0x8901e283 | key;
  part ^= 0x8ec94507 ^ key;
  part += 0x89d23300 | key;
  part ^= 0x1ff8e1dc ^ key;
  part += 0x108a004a | key;
  part ^= 0xc73d6304 ^ key;
  part += 0x43d2d3 | key;
  part ^= 0x6f78e4ff ^ key;
  csum += part;

  part = 0x45c23f9;
  part += 0x47d86097 | key;
  part ^= 0x7cb588bd ^ key;
  part += 0x9286 | key;
  part ^= 0x21d700f8 ^ key;
  part += 0xdf8e0fd9 | key;
  part ^= 0x42796c9e ^ key;
  part += 0x8b000003 | key;
  part ^= 0x3ad32a21 ^ key;
  csum += part;

  part = 0xb229a3d0;
  part += 0x47d708 | key;
  part ^= 0x10b0a91 ^ key;
  csum += part;

  part = 0x466e55a7;
  part += 0xc7880d8b | key;
  part ^= 0x44ce7067 ^ key;
  part += 0xe4 | key;
  part ^= 0x923a6d44 ^ key;
  part += 0x640047d6 | key;
  part ^= 0xa62d606c ^ key;
  part += 0x2bd1f7ae | key;
  part ^= 0x2f5621fb ^ key;
  part += 0x8b0f74ff | key;
  part ^= 0x2928b332;
  csum += part;

  part = 0x62cf369a;
  csum += part;

  return csum;
}

}  // namespace null
