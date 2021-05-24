#ifndef NULLSPACE_SPECTATE_VIEW_H_
#define NULLSPACE_SPECTATE_VIEW_H_

#include "InputState.h"
#include "Types.h"

namespace null {

struct Camera;
struct Connection;
struct Player;
struct SpriteRenderer;
struct StatBox;

constexpr u16 kInvalidSpectateId = -1;

struct SpectateView {
  Connection& connection;
  StatBox& statbox;

  u16 spectate_id = kInvalidSpectateId;
  u32 spectate_frequency = 0;
  u32 last_spectate_packet = 0;

  SpectateView(Connection& connection, StatBox& statbox);

  u32 GetFrequency();

  void Update(const InputState& input, float dt);
  void Render(Camera& ui_camera, SpriteRenderer& renderer);

  void OnAction(InputAction action);

  void SpectateSelected();
};

}  // namespace null

#endif
