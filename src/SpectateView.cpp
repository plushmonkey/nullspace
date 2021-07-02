#include "SpectateView.h"

#include <cstdio>

#include "InputState.h"
#include "PlayerManager.h"
#include "StatBox.h"
#include "Tick.h"
#include "net/Connection.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

SpectateView::SpectateView(Connection& connection, StatBox& statbox) : connection(connection), statbox(statbox) {}

u32 SpectateView::GetFrequency() {
  Player* self = statbox.player_manager.GetSelf();

  if (!self) return 0;

  return self->ship < 8 ? self->frequency : spectate_frequency;
}

void SpectateView::Update(const InputState& input, float dt) {
  Player* self = statbox.player_manager.GetSelf();

  if (!self) return;

  if (self->ship != 8) {
    spectate_id = kInvalidSpectateId;
    return;
  }

  float spectate_speed = 30.0f * dt;

  if (input.IsDown(InputAction::Afterburner)) {
    spectate_speed *= 2.5f;
  }

  bool moved = false;

  if (input.IsDown(InputAction::Left)) {
    self->position.x -= spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Right)) {
    self->position.x += spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Forward)) {
    self->position.y -= spectate_speed;
    moved = true;
  }

  if (input.IsDown(InputAction::Backward)) {
    self->position.y += spectate_speed;
    moved = true;
  }

  if (moved) {
    spectate_id = kInvalidSpectateId;
  } else if (input.IsDown(InputAction::Bullet)) {
    SpectateSelected();
  }

  if (spectate_id != kInvalidSpectateId) {
    Player* follow_player = statbox.player_manager.GetPlayerById(spectate_id);

    if (follow_player && follow_player->ship != 8) {
      self->position = follow_player->position;
    } else {
      // TODO: Get next spectator
      spectate_id = kInvalidSpectateId;
    }
  }
}

void SpectateView::Render(Camera& ui_camera, SpriteRenderer& renderer) {
  Player* self = statbox.player_manager.GetSelf();

  if (!self) return;
  if (self->ship != 8) return;

  // TODO: find real position for indicators
  float y = (ui_camera.surface_dim.y * 0.57f) + 1.0f;
  float x = ui_camera.surface_dim.x - 26;
  size_t icon_index = (self->togglables & Status_XRadar) ? 36 : 37;

  renderer.Draw(ui_camera, Graphics::icon_sprites[icon_index], Vector2f(x, y), Layer::Gauges);

  Player* follow_player = statbox.player_manager.GetPlayerById(spectate_id);

  if (follow_player && follow_player->energy > 0.0f) {
    char rows[4][64];

    sprintf(rows[0], "Engy:%-5d S2CLatency:%dms", (u32)follow_player->energy, follow_player->s2c_latency * 10);
    sprintf(rows[1], "Brst:%-2d Repl:%-2d Prtl:%-2d", follow_player->bursts, follow_player->repels,
            follow_player->portals);
    sprintf(rows[2], "Decy:%-2d Thor:%-2d", follow_player->decoys, follow_player->thors);
    sprintf(rows[3], "Wall:%-2d Rckt:%-2d", follow_player->bricks, follow_player->rockets);
    // TODO: Super and shields display

    float x = ui_camera.surface_dim.x / 2.0f;

    for (size_t i = 0; i < 4; ++i) {
      renderer.DrawText(ui_camera, rows[i], TextColor::White, Vector2f(x, (float)i * 12), Layer::Gauges);
    }
  }
}

void SpectateView::SpectateSelected() {
  Player* selected = statbox.GetSelectedPlayer();

  if (selected) {
    SpectatePlayer(*selected);
  }
}

void SpectateView::SpectatePlayer(Player& player) {
  u32 tick = GetCurrentTick();

  if (TICK_DIFF(tick, last_spectate_packet) < 10) return;

  if (player.ship != 8 && player.id != spectate_id) {
    spectate_id = player.id;
    spectate_frequency = player.frequency;
    last_spectate_packet = tick;

    connection.SendSpectateRequest(spectate_id);
  }
}

void SpectateView::OnAction(InputAction action) {
  Player* self = statbox.player_manager.GetSelf();

  if (!self || self->ship != 8) return;

  // TODO: F5 to follow ball and F4 to spectate multiple players
  if (action == InputAction::Bullet || action == InputAction::Bomb) {
    SpectateSelected();
  }
}

}  // namespace null
