#ifndef NULLSPACE_NET_CRYPT_H_
#define NULLSPACE_NET_CRYPT_H_

#include "../Random.h"
#include "../Types.h"
#include "../WorkQueue.h"

namespace null {

typedef void (*LoadTableCallback)(u32 key, u32* table, bool success, void* user);

struct GeneratorWork {
  struct ContinuumEncrypt* crypt;
  u32 key;
  u32 table[20];
  bool success;
  void* user;

  LoadTableCallback callback;
};

struct ContinuumEncrypt {
  ContinuumEncrypt(WorkQueue& work_queue) : work_queue(work_queue) {}

  size_t Encrypt(const u8* pkt, u8* dest, size_t size);
  size_t Decrypt(u8* pkt, size_t size);

  bool ExpandKey(u32 key1, u32 key2);

  void LoadTable(u32 key, void* user, LoadTableCallback callback);
  // Loads the first two steps for key expansion requests
  bool LoadTable(u32* table, u32 key);

  u32 expanded_key[20] = {};
  u32 key1 = 0;
  u32 key2 = 0;

  // TODO: This isn't great because it means only one can be running at a time. Shouldn't really need more though.
  GeneratorWork work;

  WorkQueue& work_queue;
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
