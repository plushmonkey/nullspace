#include "PacketSequencer.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../Buffer.h"
#include "../Clock.h"
#include "../Logger.h"
#include "Connection.h"

#define DEBUG_SEQUENCER

namespace null {

constexpr size_t kReliableHeaderSize = 6;
constexpr u32 kResendDelay = 300;

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
#ifdef DEBUG_SEQUENCER
    Log(LogLevel::Jabber, "Processing reliable id %d", process_queue[0].id);
#endif
    connection.ProcessPacket(process_queue[0].message, process_queue[0].size);
    std::pop_heap(process_queue, process_queue + process_queue_count--);
    ++next_reliable_process_id;
  }

  u32 current_tick = GetCurrentTick();

  // Resend timed out messages from reliable_sent
  for (size_t i = 0; i < reliable_sent_count; ++i) {
    ReliableMessage* mesg = reliable_sent + i;

    if (TICK_DIFF(current_tick, mesg->timestamp) >= kResendDelay) {
#ifdef DEBUG_SEQUENCER
      Log(LogLevel::Jabber, "******** Resending timed out message with id %d", mesg->id);
#endif
      SendReliable(connection, *mesg);
      mesg->timestamp = current_tick;
    }
  }
}

///////////// Reliable messages

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

#ifdef DEBUG_SEQUENCER
  Log(LogLevel::Jabber, "Got reliable message of id %d", id);
#endif

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

#ifdef DEBUG_SEQUENCER
  Log(LogLevel::Jabber, "Received reliable ack with id %d", id);
#endif

  for (size_t i = 0; i < reliable_sent_count; ++i) {
    ReliableMessage* mesg = reliable_sent + i;

    if (mesg->id == id) {
      // Swap last reliable sent message to this slot and decrease count
      reliable_sent[i] = reliable_sent[--reliable_sent_count];
#ifdef DEBUG_SEQUENCER
      Log(LogLevel::Jabber, "Found reliable ack in sent list.");
#endif
      return;
    }
  }
}

///////////// Small chunks

void PacketSequencer::OnSmallChunkBody(Connection& connection, u8* pkt, size_t size) {
  small_chunks.Push(perm_arena, pkt + 2, size - 2);
}

void PacketSequencer::OnSmallChunkTail(Connection& connection, u8* pkt, size_t size) {
  small_chunks.Push(perm_arena, pkt + 2, size - 2);

  ChunkData* current = small_chunks.chunks;
  assert(current);

  u8* body_data = nullptr;
  size_t body_size = small_chunks.Construct(temp_arena, &body_data);

  assert(body_data);

  small_chunks.Clear();

  // Process the full body. The body data will be freed when the temp arena is freed in the main loop.
  connection.ProcessPacket(body_data, body_size);
}

///////////// Huge chunks

void PacketSequencer::OnHugeChunk(Connection& connection, u8* pkt, size_t size) {
  u32 length = *(u32*)(pkt + 2);

  huge_chunks.Push(perm_arena, pkt + 6, size - 6);

#ifdef DEBUG_SEQUENCER
  Log(LogLevel::Jabber, "Huge chunk received (%zu / %d)", huge_chunks.size, length);
#endif

  if (huge_chunks.size >= length) {
    u8* body_data = nullptr;
    size_t body_size = huge_chunks.Construct(temp_arena, &body_data);

    assert(body_data);

    huge_chunks.Clear();

    connection.ProcessPacket(body_data, body_size);
  }
}

void PacketSequencer::OnCancelHugeChunk(Connection& connection, u8* pkt, size_t size) {
  struct {
    u8 core;
    u8 type;
  } ack = {0x00, 0x0C};

  connection.Send((u8*)&ack, 2);

  huge_chunks.Clear();
}

///////////// Chunk store

void ChunkStore::Push(MemoryArena& arena, u8* data, size_t size) {
  ChunkData* body_data = free;

  // Allocate or pop off free list
  if (!body_data) {
    body_data = memory_arena_push_type(&arena, ChunkData);
  } else {
    free = free->next;
  }

  assert(body_data);
  assert(size <= kMaxPacketSize);

  memcpy(body_data->data, data, size);
  body_data->size = size;
  body_data->next = nullptr;

  if (chunks == nullptr) {
    chunks = body_data;
  }

  if (end) {
    end->next = body_data;
  }

  end = body_data;
  this->size += size;
}

void ChunkStore::Clear() {
  ChunkData* current = chunks;

  // Push the full chunk list into the free list
  while (current) {
    ChunkData* new_free = current;
    current = current->next;

    new_free->next = free;
    free = new_free;
  }

  chunks = nullptr;
  end = nullptr;
  size = 0;
}

size_t ChunkStore::Construct(MemoryArena& arena, u8** data) {
  *data = nullptr;

  // Allocate enough space for the body
  *data = arena.Allocate(size);
  u8* write_ptr = *data;

  // Loop to write data to the body
  ChunkData* current = chunks;
  while (current) {
    memcpy(write_ptr, current->data, current->size);
    write_ptr += current->size;
    current = current->next;
  }

  return size;
}

}  // namespace null
