#ifndef NULLSPACE_NET_PACKETDISPATCHER_H_
#define NULLSPACE_NET_PACKETDISPATCHER_H_

#include <null/Types.h>
#include <null/net/Protocol.h>

namespace null {

using PacketCallback = void (*)(void* user, u8* pkt, size_t size);

struct PacketHandler {
  void* user;
  PacketCallback callback;
};

constexpr size_t kPacketMaxId = (size_t)(ProtocolS2C::Count);
constexpr size_t kPacketMaxHandlers = 64;

struct HandlerContainer {
  size_t handler_count = 0;
  PacketHandler handlers[kPacketMaxHandlers];
};

// TODO: This should probably just get rewritten as a general event dispatcher with ability to dispatch to class methods
// Currently can only dispatch to functions with a user pointer that allows for re-dispatching from that to a class
// method.
struct PacketDispatcher {
  HandlerContainer core_packets[0x15];
  HandlerContainer game_packets[kPacketMaxId];

  void Dispatch(u8* pkt, size_t size);
  void Register(ProtocolCore type, PacketCallback callback, void* user = nullptr);
  void Register(ProtocolS2C type, PacketCallback callback, void* user = nullptr);
};

}  // namespace null

#endif
