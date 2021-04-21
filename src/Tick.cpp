#include "Tick.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif

namespace null {

Tick GetCurrentTick() {
#ifdef _WIN32
  return (GetTickCount() / 10) & 0x7fffffff;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  
  u32 ticks = ts.tv_sec & 0x7fffffff;
  ticks *= 100U;
  ticks += ts.tv_nsec / 10000000;

  return ticks & 0x7fffffff;
#endif
}

}  // namespace null

