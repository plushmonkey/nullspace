#ifndef NULLSPACE_WORKQUEUE_H_
#define NULLSPACE_WORKQUEUE_H_

#include <condition_variable>
#include <mutex>
#include <thread>

#include "Memory.h"
#include "Types.h"

namespace null {

typedef void (*WorkRun)(struct Work* work);
typedef void (*WorkComplete)(struct Work* work);

struct WorkDefinition {
  WorkRun run;
  WorkComplete complete;
};

struct Work {
  WorkDefinition definition;
  void* user;
  bool valid;

  Work* next;
};

struct WorkQueue {
  MemoryArena& arena;
  volatile size_t queue_size;
  Work* queue;

  Work* free;

  WorkQueue(MemoryArena& arena);

  void Submit(WorkDefinition definition, void* user);
  void Clear();

  std::condition_variable convar;
  std::mutex mutex;
};

struct Worker {
  WorkQueue& queue;
  std::thread thread;

  Worker(WorkQueue& queue);

  void Launch();

 private:
  void Run();
};

}  // namespace null

#endif
