#include "PacketDispatcher.h"

#include <assert.h>

namespace null {

void PacketDispatcher::Dispatch(u8* pkt, size_t size) {
  u8 type = pkt[0];

  assert(type < kPacketMaxId);

  HandlerContainer* container = game_packets;

  if (type == 0) {
    type = pkt[1];

    assert(type < NULLSPACE_ARRAY_SIZE(core_packets));

    container = core_packets;
  }

  assert(type < kPacketMaxId);

  for (size_t i = 0; i < container[type].handler_count; ++i) {
    PacketHandler* handler = container[type].handlers + i;

    handler->callback(handler->user, pkt, size);
  }
}

void PacketDispatcher::Register(ProtocolCore type, PacketCallback callback, void* user) {
  size_t type_index = (size_t)type;
  PacketHandler* handler = core_packets[type_index].handlers + core_packets[type_index].handler_count++;

  handler->user = user;
  handler->callback = callback;
}

void PacketDispatcher::Register(ProtocolS2C type, PacketCallback callback, void* user) {
  size_t type_index = (size_t)type;
  PacketHandler* handler = game_packets[type_index].handlers + game_packets[type_index].handler_count++;

  handler->user = user;
  handler->callback = callback;
}

}  // namespace null
