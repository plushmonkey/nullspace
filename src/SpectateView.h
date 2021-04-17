#ifndef NULLSPACE_SPECTATE_VIEW_H_
#define NULLSPACE_SPECTATE_VIEW_H_

#include "Types.h"

namespace null {

struct Connection;
struct InputState;
struct Player;
struct StatBox;

constexpr u16 kInvalidSpectateId = -1;

struct SpectateView {
  Connection& connection;
  StatBox& statbox;

  u16 spectate_id = kInvalidSpectateId;
  u32 spectate_frequency = 0;
  u32 last_spectate_packet = 0;

  SpectateView(Connection& connection, StatBox& statbox);

  void Update(const InputState& input, float dt);

  void OnCharacterPress(int codepoint, int mods);

  void SpectateSelected();
};

}  // namespace null

#endif
