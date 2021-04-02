#include "Crypt.h"

#include <cstring>
#include <memory>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "../Checksum.h"
#include "../MD5.h"

namespace null {

static u32 rol(u32 var, u32 amount) {
  amount &= 31;
  return ((var << amount) | (var >> (32 - amount)));
}

static u32 ror(u32 var, u32 amount) {
  amount &= 31;
  return ((var >> amount) | (var << (32 - amount)));
}

void encrypt(void* target, const void* source, u32 packetlen, const u32* key) {
  const u32* decPacket = (const u32*)source;
  u32* encPacket = (u32*)target;

  u32 sizeInBlocks = packetlen / 8;
  u32 bytesRemaining = packetlen;

  u32 lastUpperhalf = 0;
  u32 lastLowerhalf = 0;

  for (u32 i = 0; i < sizeInBlocks; ++i) {
    u32 lowerhalf = decPacket[0] ^ lastLowerhalf;
    u32 upperhalf = decPacket[1] ^ lastUpperhalf;

    for (u32 j = 0; j < 20; ++j) {
      lowerhalf += key[j++];
      lowerhalf = rol(lowerhalf, upperhalf & 0xFF);

      upperhalf += key[j];
      upperhalf = rol(upperhalf, lowerhalf & 0xFF);
    }

    encPacket[0] = lowerhalf;
    encPacket[1] = upperhalf;

    encPacket += 2;
    decPacket += 2;

    lastLowerhalf = lowerhalf;
    lastUpperhalf = upperhalf;

    bytesRemaining -= 8;
  }

  if (bytesRemaining >= 4) {
    u32 block = decPacket[0] ^ lastLowerhalf;

    for (u32 i = 0; i < 20; ++i) {
      block += key[i++];
      block = rol(block, lastUpperhalf & 0xFF);

      lastUpperhalf += key[i];
    }

    encPacket[0] = block;
    ++encPacket;
    ++decPacket;

    lastLowerhalf = block;
    bytesRemaining -= 4;
  }

  u8* encPacketBytes = (u8*)encPacket;
  const u8* decPacketBytes = (const u8*)decPacket;
  for (u32 i = 0; i < bytesRemaining; ++i) {
    u8 currentByte = decPacketBytes[0] ^ lastLowerhalf;

    for (u32 j = 0; j < 20; ++j) {
      currentByte += key[j++] & 0xFF;
      currentByte += lastUpperhalf & 0xFF;

      lastUpperhalf += key[j];
    }

    encPacketBytes[0] = currentByte;

    ++encPacketBytes;
    ++decPacketBytes;

    lastLowerhalf = currentByte;
  }
}

void decrypt(void* target, const void* source, u32 packetlen, const u32* key) {
  u32* decPacket = (u32*)target;
  const u32* encPacket = (const u32*)source;

  u32 sizeInBlocks = packetlen / 8;
  u32 bytesRemaining = packetlen;

  u32 lastUpperhalf = 0;
  u32 lastLowerhalf = 0;

  for (u32 i = 0; i < sizeInBlocks; ++i) {
    u32 lowerhalf = encPacket[0];
    u32 upperhalf = encPacket[1];

    for (s32 j = 19; j >= 0; --j) {
      upperhalf = ror(upperhalf, lowerhalf & 0xFF);
      upperhalf -= key[j--];

      lowerhalf = ror(lowerhalf, upperhalf & 0xFF);
      lowerhalf -= key[j];
    }

    lowerhalf ^= lastLowerhalf;
    upperhalf ^= lastUpperhalf;

    decPacket[0] = lowerhalf;
    decPacket[1] = upperhalf;
    decPacket += 2;

    lastLowerhalf = encPacket[0];
    lastUpperhalf = encPacket[1];
    encPacket += 2;

    bytesRemaining -= 8;
  }

  if (bytesRemaining >= 4) {
    u32 roramount = lastUpperhalf;
    u32 block = encPacket[0];

    for (u32 i = 1; i < 20; i += 2) roramount += key[i];

    lastUpperhalf = roramount;

    for (s32 i = 19; i > 0; i -= 2) {
      roramount -= key[i];
      block = ror(block, roramount & 0xFF);
      block -= key[i - 1];
    }

    block ^= lastLowerhalf;

    decPacket[0] = block;
    ++decPacket;

    lastLowerhalf = encPacket[0];
    ++encPacket;

    bytesRemaining -= 4;
  }

  const u8* encPacketBytes = (const u8*)encPacket;
  u8* decPacketBytes = (u8*)decPacket;
  for (u32 i = 0; i < bytesRemaining; ++i) {
    u8 currentByte = encPacketBytes[0];
    u32 accum = lastUpperhalf;

    for (u32 j = 1; j < 20; j += 2) accum += key[j];

    lastUpperhalf = accum;

    for (s32 j = 19; j > 0; j -= 2) {
      accum -= key[j];
      currentByte -= ((key[j - 1] & 0xFF) + (accum & 0xFF));
    }

    currentByte ^= (lastLowerhalf & 0xFF);

    decPacketBytes[0] = currentByte;
    ++decPacketBytes;

    lastLowerhalf = encPacketBytes[0];
    ++encPacketBytes;
  }
}

struct expansion {
  unsigned int key;
  unsigned int table[20];

  expansion() {}
  expansion(unsigned int key) : key(key) {}
};

struct security_file {
  unsigned int version;
  expansion _expansions1[1024];  // This is always zeroed
  expansion expansions[1024];
};

std::unique_ptr<security_file> read_security_file(const char* filename) {
  std::unique_ptr<security_file> result;
  FILE* f = fopen(filename, "rb");

  if (f == nullptr) {
    return result;
  }

  result = std::make_unique<security_file>();

  fread(result.get(), sizeof(security_file), 1, f);

  fclose(f);

  return result;
}

bool generate(u32 key) {
#ifdef _WIN32
  std::string keystr = "\"generator.exe\" " + std::to_string(key);

  STARTUPINFOA info = {sizeof(info)};
  PROCESS_INFORMATION pinfo = {0};
  bool success = false;

  if (CreateProcessA("generator.exe", &keystr[0], nullptr, nullptr, false, 0, nullptr, nullptr, &info, &pinfo)) {
    WaitForSingleObject(pinfo.hProcess, 0xFFFFFFFF);
    CloseHandle(pinfo.hProcess);
    CloseHandle(pinfo.hThread);
    success = true;
  } else {
    fprintf(stderr, "Failed to create generator process. %d\n", GetLastError());
  }

  return success;
#else
  return false;
#endif
}

// Loads the first two steps for key expansion requests
bool ContinuumEncrypt::LoadTable(u32* table, u32 key) {
  // TODO: Probably use some network service as oracle instead of generator.exe

  // Use generator.exe to perform first two steps of key expansion
  if (!generate(key)) {
    return false;
  }

  auto scrty1 = read_security_file("scrty1");
  if (!scrty1) {
    return false;
  }

  memcpy(table, scrty1->expansions[0].table, sizeof(int) * 20);
  return true;
}

bool ContinuumEncrypt::ExpandKey(u32 key1, u32 key2) {
  constexpr u32 kContinuumStateMangler = 432;

  this->key1 = this->key2 = 0;

  if (!LoadTable(expanded_key, key2)) {
    return false;
  }
  
  // Perform final step of key expansion by running it through a modified MD5 hasher
  expanded_key[0] ^= key1;

  MD5_CTX ctx;
  MD5Init(&ctx, kContinuumStateMangler);
  MD5Update(&ctx, (u8*)expanded_key, sizeof(u32) * 20);

  u32 last = key1;
  for (u32 i = 0; i < 20; ++i) {
    last = (expanded_key[i] ^= ctx.buf[i & 3] + last);
  }

  this->key1 = key1;
  this->key2 = key2;

  return true;
}

void ContinuumEncrypt::Encrypt(u8* pkt, size_t size) {
  u8 encrypted[kMaxPacketSize];
  u8 source[kMaxPacketSize];
  u8 crc = crc8(pkt, size);

  source[0] = crc;
  memcpy(source + 1, pkt, size);

  // TODO: Can target and source be the same thing?
  // TODO: Max packet sizes including crc?
  encrypt(encrypted, source, (u32)size + 1, expanded_key);

  // Perform crc escape if the crc ends up being 0xFF or encrypted packet looks like connection init packet
  if (encrypted[0] == 0xFF ||
      (encrypted[0] == 0x00 && (encrypted[1] == 0x01 || encrypted[1] == 0x10 || encrypted[1] == 0x11))) {
    pkt[0] = 0xFF;
    memcpy(pkt + 1, encrypted, size + 1);
  } else {
    memcpy(pkt, encrypted, size + 1);
  }
}

void ContinuumEncrypt::Decrypt(u8* pkt, size_t size) {
  u8* src = pkt;
  // Perform crc escape if the crc ends up being 0xFF
  if ((u8)src[0] == 0xFF && ((u8)src[1] == 0x00 || (u8)src[1] == 0xFF)) {
    src++;
    size--;
  }

  u8 decrypted[kMaxPacketSize];
  decrypt(decrypted, src, (u32)size, expanded_key);

  memcpy(pkt, decrypted + 1, size - 1);

  u8 crc_check = decrypted[0];
  u8 crc = crc8(decrypted + 1, size - 1);

  // TODO: crc verify
}

}  // namespace null
