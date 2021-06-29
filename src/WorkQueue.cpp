#include "WorkQueue.h"

namespace null {

WorkQueue::WorkQueue(MemoryArena& arena) : arena(arena), queue_size(0), queue(nullptr), free(nullptr) {}

void WorkQueue::Submit(WorkDefinition definition, void* user) {
  {
    std::lock_guard<std::mutex> lock(mutex);

    Work* work = free;

    if (!work) {
      work = free = memory_arena_push_type(&arena, Work);
      work->next = nullptr;
    }

    free = free->next;

    work->definition = definition;
    work->user = user;
    work->next = queue;

    queue = work;
    ++queue_size;
  }

  convar.notify_one();
}

void WorkQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex);

  // Push everything to freelist
  Work* work = queue;
  while (work) {
    Work* current = work;
    work = work->next;

    current->next = free;
    free = current;
  }

  queue = nullptr;
  queue_size = 0;
}

void Worker::Run() {
  while (true) {
    Work* work = nullptr;

    {
      std::unique_lock<std::mutex> lock(queue.mutex);

      queue.convar.wait(lock, [this] { return this->queue.queue_size > 0; });

      work = queue.queue;
      if (!work) continue;

      // Pop work off queue
      queue.queue = work->next;
      --queue.queue_size;
    }

    work->definition.run(work);
    work->definition.complete(work);

    std::lock_guard<std::mutex> lock(queue.mutex);

    // Push work to free list
    work->next = queue.free;
    queue.free = work;
  }
}

void Worker::Launch() {
  thread = std::thread(&Worker::Run, this);
}

Worker::Worker(WorkQueue& queue) : queue(queue) {}

}  // namespace null
