#ifndef NULLSPACE_NET_CRYPT_H_
#define NULLSPACE_NET_CRYPT_H_

#include "../Types.h"
#include "../Random.h"

namespace null {

struct ContinuumEncrypt {
  size_t Encrypt(const u8* pkt, u8* dest, size_t size);
  size_t Decrypt(u8* pkt, size_t size);

  bool ExpandKey(u32 key1, u32 key2);

  // Loads the first two steps for key expansion requests
  bool LoadTable(u32* table, u32 key);

  u32 expanded_key[20] = {};
  u32 key1 = 0;
  u32 key2 = 0;
};

struct VieEncrypt {
  size_t Encrypt(const u8* pkt, u8* dest, size_t size);
  size_t Decrypt(u8* pkt, size_t size);

  bool Initialize(u32 server_key);
  bool IsValidKey(u32 server_key);

  static u32 GenerateKey();

  u32 session_key = 0;
  u32 client_key = 0;
  VieRNG rng;
  char keystream[520];
};

}  // namespace null

#endif
