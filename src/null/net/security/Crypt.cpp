#include "Crypt.h"

#include <null/Clock.h>
#include <null/Logger.h>
#include <null/net/security/Checksum.h>
#include <null/net/security/MD5.h>
//
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
#include <stdio.h>
#endif

namespace null {

static u32 rol(u32 var, u32 amount) {
  amount &= 31;
  return ((var << amount) | (var >> (32 - amount)));
}

static u32 ror(u32 var, u32 amount) {
  amount &= 31;
  return ((var >> amount) | (var << (32 - amount)));
}

size_t VieEncrypt::Encrypt(const u8* pkt, u8* dest, size_t size) {
  if (!session_key) {
    memcpy(dest, pkt, size);
    return size;
  }

  u32 ksi = 0, i = 1, IV = session_key;

  dest[0] = pkt[0];

  if (*pkt == 0) {
    if (size <= 2) {
      memcpy(dest, pkt, size);
      return size;
    }

    dest[1] = pkt[1];
    ++i;
  }

  while (i + 4 <= size) {
    *(u32*)&dest[i] = IV = (*(u32*)(pkt + i) ^ *(u32*)(keystream + ksi) ^ IV);

    i += 4;
    ksi += 4;
  }

  size_t diff = size - i;

  if (diff) {
    u32 remaining = 0;

    memcpy(&remaining, pkt + i, diff);

    remaining ^= *(u32*)(keystream + ksi) ^ IV;
    memcpy(dest + i, &remaining, diff);
  }

  return size;
}

size_t VieEncrypt::Decrypt(u8* pkt, size_t size) {
  if (!session_key) {
    return size;
  }

  u32 ksi = 0, i = 1, IV = session_key, EDX;

  if (*pkt == 0) {
    if (size <= 2) {
      return size;
    }

    ++i;
  }

  while (i + 4 <= size) {
    EDX = *(u32*)(pkt + i);

    *(u32*)&pkt[i] = *(u32*)(keystream + ksi) ^ IV ^ EDX;

    IV = EDX;
    i += 4;
    ksi += 4;
  }

  size_t diff = size - i;

  if (diff) {
    u32 remaining = 0;

    memcpy(&remaining, pkt + i, diff);

    remaining ^= *(u32*)(keystream + ksi) ^ IV;
    memcpy(pkt + i, &remaining, diff);
  }

  return size;
}

bool VieEncrypt::IsValidKey(u32 server_key) {
  return (server_key == session_key) || (server_key == client_key) || (server_key == ((~client_key) + 1));
}

bool VieEncrypt::Initialize(u32 server_key) {
  if (!IsValidKey(server_key)) return false;

  if (client_key == server_key) {
    session_key = 0;
    memset(keystream, 0, 520);
  } else {
    session_key = server_key;
    u16* stream = (u16*)keystream;

    rng.Seed(session_key);
    for (u32 i = 0; i < 520 / 2; ++i) {
      stream[i] = (u16)rng.GetNextEncrypt();
    }
  }

  return true;
}

u32 VieEncrypt::GenerateKey() {
  u32 edx = GetCurrentTick() * 0xCCCCCCCD;

  srand(GetCurrentTick());

  u32 res = ((rand() % 65535) << 16) + (edx >> 3) + (rand() % 65535);

  res = (res ^ edx) - edx;

  if (res <= 0x7fffffff) res = ~res + 1;

  return res;
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

void ContinuumEncrypt::FinalizeExpansion(u32 key) {
  constexpr u32 kContinuumStateMangler = 432;

  this->key1 = key;

  // Perform final step of key expansion by running it through a modified MD5 hasher
  expanded_key[0] ^= key1;

  MD5_CTX ctx;
  MD5Init(&ctx, kContinuumStateMangler);
  MD5Update(&ctx, (u8*)expanded_key, sizeof(u32) * 20);

  u32 last = key1;
  for (u32 i = 0; i < 20; ++i) {
    last = (expanded_key[i] ^= ctx.buf[i & 3] + last);
  }

  this->state = State::Waiting;
}

size_t ContinuumEncrypt::Encrypt(const u8* pkt, u8* dest, size_t size) {
  u8 source[kMaxPacketSize + 1];
  u8 crc = crc8(pkt, size);

  source[0] = crc;
  memcpy(source + 1, pkt, size++);

  // TODO: Can target and source be the same thing?
  // TODO: Max packet sizes including crc?
  encrypt(dest, source, (u32)size, expanded_key);

  // Perform crc escape if the crc ends up being 0xFF or encrypted packet looks like connection init packet
  if (dest[0] == 0xFF || (dest[0] == 0x00 && (dest[1] == 0x01 || dest[1] == 0x10 || dest[1] == 0x11))) {
    memmove(dest + 1, dest, size);
    dest[0] = 0xFF;
    ++size;
  }

  return size;
}

size_t ContinuumEncrypt::Decrypt(u8* pkt, size_t size) {
  u8* src = pkt;
  // Perform crc escape if the crc ends up being 0xFF
  if ((u8)src[0] == 0xFF && ((u8)src[1] == 0x00 || (u8)src[1] == 0xFF)) {
    src++;
    size--;
  }

  u8 decrypted[kMaxPacketSize];
  decrypt(decrypted, src, (u32)size, expanded_key);

  size--;

  memcpy(pkt, decrypted + 1, size);
 
  u8 crc_check = decrypted[0];
  u8 crc = crc8(decrypted + 1, size);

  if (crc != crc_check) {
    u8 repeat_crc = crc8_repeat(pkt[0], size);
    
    if (repeat_crc != crc_check) {
      Log(LogLevel::Debug, "Discarding packet with bad crc.");
      return 0;
    }
  }

  return size;
}

}  // namespace null
