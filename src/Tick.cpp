#include "Tick.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace null {

Tick GetCurrentTick() {
#ifdef _WIN32
  return (GetTickCount() / 10) & 0x7fffffff;
#else
  static_assert(0, "GetCurrentTick not implemented");
#endif
}

}  // namespace null
