#ifndef NULLSPACE_RANDOM_H_
#define NULLSPACE_RANDOM_H_

#include "Types.h"

namespace null {

struct VieRNG {
  s32 seed;

  inline void Seed(u32 seed) { this->seed = seed; }

  inline u32 GetNext() {
    seed = ((seed % 0x1F31D) * 0x41A7 - ((seed / 0x1F31D) * 0xB14)) + 0x7B;
    if (seed < 1) {
      seed += 0x7FFFFFFF;
    }
    return (u32)seed;
  }
};

}  // namespace null

#endif
