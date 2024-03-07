#ifndef NULLSPACE_SPECTATE_VIEW_H_
#define NULLSPACE_SPECTATE_VIEW_H_

#include <null/InputState.h>
#include <null/Types.h>

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
  u32 render_extra_lines = 0;

  SpectateView(Connection& connection, StatBox& statbox);

  u32 GetFrequency();
  // Gets the ship of the player or the spectated player for visibility settings such as SeeBombLevel and SeeMines.
  u8 GetVisibilityShip();
  u16 GetPlayerId();

  // Returns true if the spectate target changes.
  bool Update(const InputState& input, float dt);
  void Render(Camera& ui_camera, SpriteRenderer& renderer);

  // Returns true if the spectate target changes.
  bool OnAction(InputAction action);

  // Returns true if the spectate target changes.
  bool SpectateSelected();
  // Returns true if the spectate target changes.
  bool SpectatePlayer(Player& player);
};

}  // namespace null

#endif
