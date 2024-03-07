#ifndef NULLSPACE_NET_SECURITY_SECURITYSOLVER_H_
#define NULLSPACE_NET_SECURITY_SECURITYSOLVER_H_

#include <null/Types.h>
#include <null/WorkQueue.h>
#include <null/net/Socket.h>
//
#include <functional>
#include <mutex>

namespace null {

using SecurityCallback = std::function<void(u32* data)>;

struct SecurityNetworkService {
  char ip[64];
  u16 port;

  SecurityNetworkService(const char* ip, u16 port);

  SocketType Connect();
};

enum class SecurityWorkState {
  Idle,
  Working,
  Success,
  Failure,
};

enum class SecurityRequestType { Expansion, Checksum };

struct SecurityNetworkWork {
  union {
    struct {
      u32 key2;
      u32 table[20];
    } expansion;
    struct {
      u32 key;
      u32 checksum;
    } checksum;
  };

  SocketType socket;
  SecurityCallback callback;
  SecurityRequestType type;
  SecurityWorkState state;
  struct SecuritySolver* solver;

  SecurityNetworkWork() {}
  ~SecurityNetworkWork() {}
};

struct SecuritySolver {
  WorkQueue& work_queue;

  SecurityNetworkService service;

  std::mutex mutex;

  SecurityNetworkWork work[16];

  SecuritySolver(WorkQueue& work_queue, const char* service_ip, u16 service_port);

  void ExpandKey(u32 key2, SecurityCallback callback);
  void GetChecksum(u32 key, SecurityCallback callback);

  SecurityNetworkWork* AllocateWork();
  void FreeWork(SecurityNetworkWork* security_work);

  void ClearWork();
};

}  // namespace null

#endif
