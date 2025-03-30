#ifndef NULLSPACE_NET_PACKETSEQUENCER_H_
#define NULLSPACE_NET_PACKETSEQUENCER_H_

#include <null/Memory.h>
#include <null/Types.h>

namespace null {

struct Connection;

struct ReliableMessage {
  size_t size;
  u32 id;
  u32 timestamp;

  u8 message[kMaxPacketSize];

  // Flipped comparison operator for heap
  bool operator<(const ReliableMessage& other) const { return id > other.id; }
};

struct ChunkData {
  u8 data[kMaxPacketSize];
  size_t size;

  ChunkData* next;
};

struct ChunkStore {
  // Beginning of chunk data stored as linked list
  ChunkData* chunks = nullptr;
  // End of chunk data to make appending faster
  ChunkData* end = nullptr;
  // Free list for chunk data
  ChunkData* free = nullptr;
  // Current size so far
  size_t size = 0;

  void Push(MemoryArena& arena, u8* data, size_t size);
  void Clear();

  size_t Construct(MemoryArena& arena, u8** data);
};

struct OutboundAckSet {
  size_t count = 0;
  u32 ids[256];
  inline void Add(u32 id) {
    if (count >= NULLSPACE_ARRAY_SIZE(ids)) return;
    ids[count++] = id;
  }
};

constexpr size_t kReliableQueueSize = 256;
struct PacketSequencer {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;

  // Next sequence number to process from the server
  u32 next_reliable_process_id = 0;
  // Next sequence number used by this client
  u32 next_reliable_id = 0;

  size_t reliable_sent_count = 0;
  // The reliable messages that were sent and are waiting to be acknowledged.
  ReliableMessage reliable_sent[kReliableQueueSize];

  size_t process_queue_count = 0;
  // The reliable messages there were received and are waiting to be processed in order.
  ReliableMessage process_queue[kReliableQueueSize];

  OutboundAckSet outbound_acks;

  ChunkStore small_chunks;
  ChunkStore huge_chunks;

  PacketSequencer(MemoryArena& perm_arena, MemoryArena& temp_arena) : perm_arena(perm_arena), temp_arena(temp_arena) {}

  void Tick(Connection& connection);

  void ProcessOutboundAcks(Connection& connection);

  void SendReliableMessage(Connection& connection, u8* pkt, size_t size);
  void OnReliableMessage(Connection& connection, u8* pkt, size_t size);
  void OnReliableAck(Connection& connection, u8* pkt, size_t size);

  void OnSmallChunkBody(Connection& connection, u8* pkt, size_t size);
  void OnSmallChunkTail(Connection& connection, u8* pkt, size_t size);

  void OnHugeChunk(Connection& connection, u8* pkt, size_t size);
  void OnCancelHugeChunk(Connection& connection, u8* pkt, size_t size);
};

}  // namespace null

#endif
