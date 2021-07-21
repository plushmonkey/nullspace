#ifndef NULLSPACE_CLOCK_H_
#define NULLSPACE_CLOCK_H_

#include "Types.h"

namespace null {

using Tick = u32;

#define TICK_DIFF(a, b) ((signed int)(((a) << 1) - ((b) << 1)) >> 1)
#define TICK_GT(a, b) (TICK_DIFF(a, b) > 0)

Tick GetCurrentTick();

constexpr s64 kTickDurationMicro = 10000;

u64 GetMicrosecondTick();

}  // namespace null

#endif