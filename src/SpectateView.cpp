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
  if (me->ship != 8) {
    spectate_id = kInvalidSpectateId;
    return;
  }

  float spectate_speed = 30.0f;

  if (input.IsDown(InputAction::Bullet)) {
    SpectateSelected();
  }

  if (input.IsDown(InputAction::Afterburner)) {
    spectate_speed *= 2.0f;
  }

  if (input.IsDown(InputAction::Left)) {
    me->position -= Vector2f(spectate_speed, 0) * dt;
    spectate_id = kInvalidSpectateId;
  }

  if (input.IsDown(InputAction::Right)) {
    me->position += Vector2f(spectate_speed, 0) * dt;
    spectate_id = kInvalidSpectateId;
  }

  if (input.IsDown(InputAction::Forward)) {
    me->position -= Vector2f(0, spectate_speed) * dt;
    spectate_id = kInvalidSpectateId;
  }

  if (input.IsDown(InputAction::Backward)) {
    me->position += Vector2f(0, spectate_speed) * dt;
    spectate_id = kInvalidSpectateId;
  }

  if (spectate_id != -1) {
    Player* follow_player = statbox.player_manager.GetPlayerById(spectate_id);

    if (follow_player) {
      me->position = follow_player->position;
    } else {
      spectate_id = kInvalidSpectateId;
    }
  }
}

void SpectateView::SpectateSelected() {
  Player* selected = statbox.GetSelectedPlayer();

  if (selected && selected->ship != 8) {
    spectate_id = selected->id;
    spectate_frequency = selected->frequency;
  }
}

void SpectateView::OnCharacterPress(int codepoint, int mods) {
  if (codepoint == NULLSPACE_KEY_CONTROL) {
    SpectateSelected();
  }
}

}  // namespace null
