#include <cstdio>

#include "Buffer.h"
#include "Math.h"
#include "Memory.h"
#include "net/Connection.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace null {


const char* kPlayerName = "nullspace";
const char* kPlayerPassword = "none";

#if 1
// Local
const char* kServerIp = "127.0.0.1";
const u16 kServerPort = 5000;
#else
// Hyperspace
const char* kServerIp = "162.248.95.143";
const u16 kServerPort = 5005;
#endif

void run() {
  constexpr size_t kPermanentSize = Megabytes(32);
  constexpr size_t kTransientSize = Megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  Connection* connection = memory_arena_construct_type(&perm_arena, Connection, perm_arena, trans_arena);
  
  null::ConnectResult result = connection->Connect(kServerIp, kServerPort);

  if (result != null::ConnectResult::Success) {
    fprintf(stderr, "Failed to connect. Error: %d\n", (int)result);
    return;
  }

  // Send Continuum encryption request
  NetworkBuffer buffer(perm_arena, kMaxPacketSize);

  buffer.WriteU8(0x00); // Core
  buffer.WriteU8(0x01); // Encryption request
  buffer.WriteU32(0x00000000); // Key
  buffer.WriteU16(0x11); // Version

  connection->Send(buffer);

  while (connection->connected) {
    connection->Tick();
    trans_arena.Reset();
  }
}

}  // namespace null

#include "net/Crypt.h"
int main(void) {
  null::run();

  return 0;
}
