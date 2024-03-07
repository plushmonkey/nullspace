#pragma once

#ifndef NULLSPACE_TYPES_H_
#define NULLSPACE_TYPES_H_

#include <stddef.h>
#include <stdint.h>

#define NULLSPACE_ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

namespace null {

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using b32 = u32;

constexpr size_t kMaxPacketSize = 520;

}  // namespace null

#endif
