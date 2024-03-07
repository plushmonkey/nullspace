#ifndef NULLSPACE_RANDOM_H_
#define NULLSPACE_RANDOM_H_

#include <null/Types.h>

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

  inline u16 GetNextEncrypt() {
    u32 old_seed = seed;

    u32 new_seed = (s32)(((s64)old_seed * 0x834E0B5F) >> 48);
    new_seed = new_seed + (new_seed >> 31);

    new_seed = ((old_seed % 0x1F31D) * 0x41A7) - (new_seed * 0xB14) + 0x7B;
    if ((s32)new_seed < 1) {
      new_seed += 0x7FFFFFFF;
    }
    seed = (s32)new_seed;
    return (u16)seed;
  }
};

}  // namespace null

#endif
