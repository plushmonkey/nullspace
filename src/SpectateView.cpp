#include "SpectateView.h"

#include "InputState.h"
#include "PlayerManager.h"
#include "StatBox.h"
#include "net/Connection.h"

namespace null {

SpectateView::SpectateView(Connection& connection, StatBox& statbox) : connection(connection), statbox(statbox) {}

void SpectateView::Update(const InputState& input, float dt) {
  Player* me = statbox.player_manager.GetSelf();

  if (!me) return;
  if (me->ship != 8) return;

  float spectate_speed = 30.0f;

  if (input.IsDown(InputAction::Bullet)) {
    SpectateSelected();
  }

  if (input.IsDown(InputAction::Afterburner)) {
    spectate_speed *= 2.0f;
  }

  if (input.IsDown(InputAction::Left)) {
    me->position -= Vector2f(spectate_speed, 0) * dt;
    follow_player = nullptr;
  }

  if (input.IsDown(InputAction::Right)) {
    me->position += Vector2f(spectate_speed, 0) * dt;
    follow_player = nullptr;
  }

  if (input.IsDown(InputAction::Forward)) {
    me->position -= Vector2f(0, spectate_speed) * dt;
    follow_player = nullptr;
  }

  if (input.IsDown(InputAction::Backward)) {
    me->position += Vector2f(0, spectate_speed) * dt;
    follow_player = nullptr;
  }

  if (follow_player) {
    me->position = follow_player->position;
  }
}

void SpectateView::SpectateSelected() {
  if (statbox.selected_player && statbox.selected_player->ship != 8) {
    follow_player = statbox.selected_player;
    spectate_frequency = follow_player->frequency;
  }
}

void SpectateView::OnCharacterPress(int codepoint, bool control) {
  if (codepoint == NULLSPACE_KEY_CONTROL) {
    SpectateSelected();
  }
}

}  // namespace null
