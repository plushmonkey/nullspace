#ifndef NULLSPACE_CLOCK_H_
#define NULLSPACE_CLOCK_H_

#include "Types.h"

namespace null {

using Tick = u32;

#define TICK_DIFF(a, b) ((signed int)(((a) << 1) - ((b) << 1)) >> 1)
#define TICK_GT(a, b) (TICK_DIFF(a, b) > 0)
#define TICK_GTE(a, b) (TICK_DIFF(a, b) >= 0)
#define MAKE_TICK(a) ((a) & 0x7FFFFFFF)

#define SMALL_TICK_DIFF(a, b) ((signed short)(((a) << 1) - ((b) << 1)) >> 1)
#define SMALL_TICK_GT(a, b) (SMALL_TICK_DIFF(a, b) > 0)
#define SMALL_TICK_GTE(a, b) (SMALL_TICK_DIFF(a, b) >= 0)

constexpr u16 kInvalidSmallTick = 0xFFFF;

Tick GetCurrentTick();

constexpr s64 kTickDurationMicro = 10000;

u64 GetMicrosecondTick();

}  // namespace null

#endif
