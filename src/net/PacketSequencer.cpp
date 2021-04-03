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
const u32 kResendDelay = 300;

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
    connection.ProcessPacket(process_queue[0].message, process_queue[0].size);
    std::pop_heap(process_queue, process_queue + process_queue_count--);
    ++next_reliable_process_id;
  }

  u32 current_tick = GetCurrentTick();

  // Resend timed out messages from reliable_sent
  for (int i = 0; i < reliable_sent_count; ++i) {
    ReliableMessage* mesg = reliable_sent + i;

    if (TICK_DIFF(current_tick, mesg->timestamp) >= kResendDelay) {
      printf("******** Resending timed out message with id %d\n", mesg->id);
      SendReliable(connection, *mesg);
      mesg->timestamp = current_tick;
    }
  }
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

  printf("Got reliable message of id %d\n", id);

  u8 ack_pkt[6];
  NetworkBuffer buffer(ack_pkt, 6);

  buffer.WriteU8(0x00);
  buffer.WriteU8(0x04);
  buffer.WriteU32(id);

  // Send acknowledgement
  connection.Send(buffer);

  // This was already processed
  if (id < next_reliable_process_id) {
    return;
  }

  for (size_t i = 0; i < process_queue_count; ++i) {
    ReliableMessage* mesg = process_queue + i;

    // Don't add it to the process list if it is already there
    if (mesg->id == id) {
      return;
    }
  }

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
