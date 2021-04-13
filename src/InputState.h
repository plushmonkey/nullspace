#ifndef NULLSPACE_INPUTSTATE_H_
#define NULLSPACE_INPUTSTATE_H_

#include "Types.h"

namespace null {

#define NULLSPACE_KEY_BACKSPACE 8
#define NULLSPACE_KEY_ENTER 10
#define NULLSPACE_KEY_ESCAPE 27

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
  DisplayMap,
  ChatDisplay,
};

using CharacterCallback = void (*)(void* user, char c, bool control);

struct InputState {
  u32 actions = 0;
  CharacterCallback callback = nullptr;
  void* user = nullptr;

  void Clear() { actions = 0; }

  void SetAction(InputAction action, bool value) {
    size_t action_bit = (size_t)action;

    if (value) {
      actions |= (1 << action_bit);
    } else {
      actions &= ~(1 << action_bit);
    }
  }

  void OnCharacter(char c, bool control = false) {
    if (callback) {
      callback(user, c, control);
    }
  }

  void SetCallback(CharacterCallback callback, void* user) {
    this->user = user;
    this->callback = callback;
  }

  bool IsDown(InputAction action) const { return actions & (1 << (size_t)action); }
};

}  // namespace null

#endif
