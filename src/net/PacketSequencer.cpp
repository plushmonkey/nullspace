#include "PacketSequencer.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../Buffer.h"
#include "../Tick.h"
#include "Connection.h"

namespace null {

size_t kReliableHeaderSize = 6;

void SendReliable(Connection& connection, ReliableMessage& mesg) {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  buffer.WriteU8(0x00);
  buffer.WriteU8(0x03);
  buffer.WriteU32(mesg.id);
  buffer.WriteString((char*)mesg.message, mesg.size);

  connection.Send(buffer);
}

void PacketSequencer::Tick(Connection& connection) {
  std::make_heap(process_queue, process_queue + process_queue_count);

  while (process_queue_count > 0 && process_queue[0].id == next_reliable_process_id) {
    // Process
    printf("Processing reliable id %d\n", process_queue[0].id);

    std::pop_heap(process_queue, process_queue + process_queue_count--);
    ++next_reliable_process_id;
  }

  // TODO: Resend timed out messages from reliable_sent
}

void PacketSequencer::SendReliableMessage(Connection& connection, u8* pkt, size_t size) {
  assert(size + kReliableHeaderSize <= kMaxPacketSize);

  ReliableMessage* mesg = reliable_sent + reliable_sent_count++;
  mesg->id = next_reliable_id++;
  mesg->size = size;
  mesg->timestamp = GetCurrentTick();
  memcpy(mesg->message, pkt, size);

  SendReliable(connection, *mesg);
}

void PacketSequencer::OnReliableMessage(Connection& connection, u8* pkt, size_t size) {
  u32 id = *(u32*)(pkt + 2);

  struct {
    u8 core;
    u8 type;
    u32 id;
  } ack_pkt = {0x00, 0x04, id};

  // Send acknowledgement
  connection.Send((u8*)&ack_pkt, sizeof(ack_pkt));

  ReliableMessage* mesg = process_queue + process_queue_count++;

  mesg->id = id;
  mesg->size = size - kReliableHeaderSize;
  mesg->timestamp = GetCurrentTick();
  memcpy(mesg->message, pkt + kReliableHeaderSize, mesg->size);

  std::push_heap(process_queue, process_queue + process_queue_count);
}

void PacketSequencer::OnReliableAck(Connection& connection, u8* pkt, size_t size) {
  u32 id = *(u32*)(pkt + 2);

  printf("Received reliable ack with id %d\n", id);

  for (size_t i = 0; i < reliable_sent_count; ++i) {
    ReliableMessage* mesg = reliable_sent + i;

    if (mesg->id == id) {
      // Swap last reliable sent message to this slot and decrease count
      reliable_sent[i] = reliable_sent[--reliable_sent_count];
      printf("Found reliable ack in sent list.\n");
      return;
    }
  }
}

}  // namespace null
