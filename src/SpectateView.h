#ifndef NULLSPACE_SPECTATE_VIEW_H_
#define NULLSPACE_SPECTATE_VIEW_H_

#include "Types.h"

namespace null {

struct Camera;
struct Connection;
struct InputState;
struct Player;
struct StatBox;

struct SpectateView {
  Connection& connection;
  StatBox& statbox;

  Player* follow_player = nullptr;
  u32 spectate_frequency = 0;

  SpectateView(Connection& connection, StatBox& statbox);

  void Update(const InputState& input, float dt);
  void OnCharacterPress(int codepoint, bool control);

  void SpectateSelected();
};

}  // namespace null

#endif
