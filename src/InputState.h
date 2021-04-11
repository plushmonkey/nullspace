#ifndef NULLSPACE_INPUTSTATE_H_
#define NULLSPACE_INPUTSTATE_H_

#include "Types.h"

namespace null {

enum class InputAction {
  Left,
  Right,
  Forward,
  Backward,
  Afterburner,
  Bomb,
  Bullet,
  Mine,
  Thor,
  Burst,
  Multifire,
  Antiwarp,
  Stealth,
  Cloak,
  XRadar,
  Repel,
  Warp,
  Portal,
  Decoy,
  Rocket,
  Brick,
  Attach,
  PlayerListCycle,
  PlayerListPrevious,
  PlayerListNext,
  PlayerListPreviousPage,
  PlayerListNextPage,
  Play,
  DisplayMap
};

struct InputState {
  u32 actions = 0;

  void Clear() { actions = 0; }

  void SetAction(InputAction action, bool value) {
    size_t action_bit = (size_t)action;

    if (value) {
      actions |= (1 << action_bit);
    } else {
      actions &= ~(1 << action_bit);
    }
  }

  bool IsDown(InputAction action) const { return actions & (1 << (size_t)action); }
};

}  // namespace null

#endif
