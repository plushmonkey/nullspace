#ifndef NULLSPACE_NET_SOCKET_H_
#define NULLSPACE_NET_SOCKET_H_

namespace null {

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

}  // namespace null

#endif
