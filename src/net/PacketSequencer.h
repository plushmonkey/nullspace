#ifndef NULLSPACE_NET_PACKETSEQUENCER_H_
#define NULLSPACE_NET_PACKETSEQUENCER_H_

#include "../Types.h"

namespace null {

struct ReliableMessage {
  size_t size;
  u32 id;
  u32 timestamp;

  u8 message[kMaxPacketSize];

  // Flipped comparison operator for heap
  bool operator<(const ReliableMessage& other) { return id > other.id; }
};

constexpr size_t kReliableQueueSize = 256;

struct Connection;

struct PacketSequencer {
  // Next sequence number to process from the server
  u32 next_reliable_process_id = 1;
  // Next sequence number used by this client
  u32 next_reliable_id = 1;

  size_t reliable_sent_count = 0;
  // The reliable messages that were sent and are waiting to be acknowledged.
  ReliableMessage reliable_sent[kReliableQueueSize];

  size_t process_queue_count = 0;
  // The reliable messages there were received and are waiting to be processed in order.
  ReliableMessage process_queue[kReliableQueueSize];

  void SendReliableMessage(Connection& connection, u8* pkt, size_t size);
  void Tick(Connection& connection);

  void OnReliableMessage(Connection& connection, u8* pkt, size_t size);
  void OnReliableAck(Connection& connection, u8* pkt, size_t size);
};

}  // namespace null

#endif
