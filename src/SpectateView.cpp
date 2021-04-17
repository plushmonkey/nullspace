#include "SpectateView.h"

#include <cstdio>

#include "InputState.h"
#include "PlayerManager.h"
#include "StatBox.h"
#include "Tick.h"
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

  s32 spectate_speed = (s32)(480000 * dt);

  if (input.IsDown(InputAction::Afterburner)) {
    spectate_speed *= 2;
  }

  bool moved = false;

  if (input.IsDown(InputAction::Left)) {
    me->position.x -= spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Right)) {
    me->position.x += spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Forward)) {
    me->position.y -= spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Backward)) {
    me->position.y += spectate_speed;
    moved = true;
  }

  if (moved) {
    spectate_id = kInvalidSpectateId;
  } else if (input.IsDown(InputAction::Bullet)) {
    SpectateSelected();
  }

  if (spectate_id != kInvalidSpectateId) {
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
  u32 tick = GetCurrentTick();

  if (TICK_DIFF(tick, last_spectate_packet) < 10) return;

  if (selected && selected->ship != 8 && selected->id != spectate_id) {
    spectate_id = selected->id;
    spectate_frequency = selected->frequency;
    last_spectate_packet = tick;

#pragma pack(push, 1)
    struct {
      u8 type;
      u16 pid;
    } spectate_request = {0x08, spectate_id};
#pragma pack(pop)

    connection.packet_sequencer.SendReliableMessage(connection, (u8*)&spectate_request, sizeof(spectate_request));
  }
}

void SpectateView::OnCharacterPress(int codepoint, int mods) {
  if (codepoint == NULLSPACE_KEY_CONTROL) {
    SpectateSelected();
  }
}

}  // namespace null
