#include "SecuritySolver.h"

#include <assert.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>
#define WSAEWOULDBLOCK EWOULDBLOCK
#define closesocket close
#endif

namespace null {

#pragma pack(push, 1)
enum class RequestType : u8 { Keystream, Checksum };
enum class ResponseType : u8 { Keystream, Checksum };

struct KeystreamRequestPacket {
  RequestType type;
  u32 key2;
};

struct KeystreamResponsePacket {
  ResponseType type;
  u32 key2;
  u32 table[20];
};

struct ChecksumRequestPacket {
  RequestType type;
  u32 key;
};

struct ChecksumResponsePacket {
  ResponseType type;
  u32 key;
  u32 checksum;
};
#pragma pack(pop)

SecurityNetworkService::SecurityNetworkService(const char* service_ip, u16 service_port) {
  strcpy(this->ip, service_ip);
  this->port = service_port;
}

SocketType SecurityNetworkService::Connect() {
  struct addrinfo hints = {0}, *result = nullptr;

  SocketType socket = -1;

  if ((socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    return -1;
  }

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char str_port[16];
  sprintf(str_port, "%d", port);

  if (getaddrinfo(this->ip, str_port, &hints, &result) != 0) {
    closesocket(socket);
    return -1;
  }

  struct addrinfo* ptr = nullptr;
  for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    struct sockaddr_in* sockaddr = (struct sockaddr_in*)ptr->ai_addr;

    if (::connect(socket, (struct sockaddr*)sockaddr, sizeof(struct sockaddr_in)) == 0) {
      break;
    }
  }

  freeaddrinfo(result);

  if (!ptr) {
    closesocket(socket);
    return -1;
  }

  return socket;
}

void ExpansionWorkRun(Work* work) {
  SecurityNetworkWork* expansion_work = (SecurityNetworkWork*)work->user;
  SecurityNetworkService* service = &expansion_work->solver->service;

  SocketType socket = service->Connect();

  if (socket == -1) {
    return;
  }

  KeystreamRequestPacket request;
  request.type = RequestType::Keystream;
  request.key2 = expansion_work->expansion.key2;

  if (send(socket, (const char*)&request, sizeof(request), 0) != sizeof(request)) {
    return;
  }

  char buffer[256];

  int bytes_received = recv(socket, buffer, sizeof(buffer), 0);
  if (bytes_received <= 0) {
    closesocket(socket);
    return;
  }

  if (buffer[0] != (char)ResponseType::Keystream) {
    return;
  }

  assert(bytes_received == sizeof(KeystreamResponsePacket));
  if (bytes_received == sizeof(KeystreamResponsePacket)) {
    KeystreamResponsePacket* response = (KeystreamResponsePacket*)buffer;

    memcpy(expansion_work->expansion.table, response->table, sizeof(expansion_work->expansion.table));

    expansion_work->state = SecurityWorkState::Success;
  }

  closesocket(socket);
}

void ExpansionWorkComplete(Work* work) {
  SecurityNetworkWork* expansion_work = (SecurityNetworkWork*)work->user;

  u32* table = nullptr;

  if (expansion_work->state == SecurityWorkState::Success) {
    table = expansion_work->expansion.table;
  }

  expansion_work->callback(table);

  expansion_work->solver->FreeWork(expansion_work);
}

const WorkDefinition kExpansionDefinition = {ExpansionWorkRun, ExpansionWorkComplete};

void ChecksumWorkRun(Work* work) {
  SecurityNetworkWork* checksum_work = (SecurityNetworkWork*)work->user;
  SecurityNetworkService* service = &checksum_work->solver->service;

  SocketType socket = service->Connect();

  if (socket == -1) {
    return;
  }

  ChecksumRequestPacket request;
  request.type = RequestType::Checksum;
  request.key = checksum_work->checksum.key;

  if (send(socket, (const char*)&request, sizeof(request), 0) != sizeof(request)) {
    return;
  }

  char buffer[256];

  int bytes_received = recv(socket, buffer, sizeof(buffer), 0);
  if (bytes_received <= 0) {
    closesocket(socket);
    return;
  }

  if (buffer[0] != (char)ResponseType::Checksum) {
    return;
  }

  assert(bytes_received == sizeof(ChecksumResponsePacket));
  if (bytes_received == sizeof(ChecksumResponsePacket)) {
    ChecksumResponsePacket* response = (ChecksumResponsePacket*)buffer;

    checksum_work->checksum.checksum = response->checksum;

    checksum_work->state = SecurityWorkState::Success;
  }

  closesocket(socket);
}

void ChecksumWorkComplete(Work* work) {
  SecurityNetworkWork* checksum_work = (SecurityNetworkWork*)work->user;

  u32* checksum = nullptr;

  if (checksum_work->state == SecurityWorkState::Success) {
    checksum = &checksum_work->checksum.checksum;
  }

  checksum_work->callback(checksum);

  checksum_work->solver->FreeWork(checksum_work);
}

const WorkDefinition kChecksumDefinition = {ChecksumWorkRun, ChecksumWorkComplete};

SecuritySolver::SecuritySolver(WorkQueue& work_queue, const char* service_ip, u16 service_port)
    : work_queue(work_queue), service(service_ip, service_port) {
  memset(work, 0, sizeof(work));
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(work); ++i) {
    work[i].state = SecurityWorkState::Idle;
  }
}

void SecuritySolver::ExpandKey(u32 key2, SecurityCallback callback) {
  SecurityNetworkWork* work = AllocateWork();

  work->state = SecurityWorkState::Working;
  work->type = SecurityRequestType::Expansion;
  work->expansion.key2 = key2;
  work->callback = callback;
  work->solver = this;

  work_queue.Submit(kExpansionDefinition, work);
}

void SecuritySolver::GetChecksum(u32 key, SecurityCallback callback) {
  SecurityNetworkWork* work = AllocateWork();

  work->state = SecurityWorkState::Working;
  work->type = SecurityRequestType::Checksum;
  work->checksum.key = key;
  work->callback = callback;
  work->solver = this;

  work_queue.Submit(kChecksumDefinition, work);
}

SecurityNetworkWork* SecuritySolver::AllocateWork() {
  std::lock_guard<std::mutex> guard(mutex);

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(work); ++i) {
    if (work[i].state == SecurityWorkState::Idle) {
      work[i].state = SecurityWorkState::Working;
      return work + i;
    }
  }

  return nullptr;
}

void SecuritySolver::FreeWork(SecurityNetworkWork* security_work) {
  std::lock_guard<std::mutex> guard(mutex);

  security_work->state = SecurityWorkState::Idle;
}

}  // namespace null
