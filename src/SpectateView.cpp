#include "SpectateView.h"

#include <cstdio>

#include "Clock.h"
#include "InputState.h"
#include "PlayerManager.h"
#include "StatBox.h"
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

u8 SpectateView::GetVisibilityShip() {
  Player* self = statbox.player_manager.GetSelf();

  if (!self) return 0;

  u32 visibility_ship = self->ship;
  if (visibility_ship == 8 && spectate_id != kInvalidSpectateId) {
    Player* spectate_player = statbox.player_manager.GetPlayerById(spectate_id);

    if (spectate_player) {
      visibility_ship = spectate_player->ship;
    }
  }

  return visibility_ship;
}

u16 SpectateView::GetPlayerId() {
  if (spectate_id != kInvalidSpectateId) {
    return spectate_id;
  }

  return statbox.player_manager.player_id;
}

bool SpectateView::Update(const InputState& input, float dt) {
  Player* self = statbox.player_manager.GetSelf();

  if (!self) return false;

  if (self->ship != 8) {
    spectate_id = kInvalidSpectateId;
    return false;
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

  u16 previous_spectate_id = spectate_id;

  if (moved) {
    if (previous_spectate_id != kInvalidSpectateId) {
      connection.SendSpectateRequest(kInvalidSpectateId);
    }

    spectate_id = kInvalidSpectateId;
  } else if (input.IsDown(InputAction::Bullet)) {
    SpectateSelected();
  }

  if (spectate_id != kInvalidSpectateId) {
    Player* follow_player = statbox.player_manager.GetPlayerById(spectate_id);

    if (follow_player && follow_player->ship != 8) {
      // Deviate from Continuum behavior for an improved spectator experience. Don't switch view to 0, 0 on player
      // death. Just continue to watch where they just died because that's where the action is.
      if (statbox.player_manager.IsSynchronized(*follow_player) && follow_player->position != Vector2f(0, 0)) {
        self->position = follow_player->position;
      }
    } else {
      // TODO: Get next spectator
      spectate_id = kInvalidSpectateId;
    }
  }

  return spectate_id != previous_spectate_id;
}

void SpectateView::Render(Camera& ui_camera, SpriteRenderer& renderer) {
  Player* self = statbox.player_manager.GetSelf();

  render_extra_lines = 0;

  if (!self) return;
  if (self->ship != 8) return;

  // TODO: find real position for indicators
  float y = (ui_camera.surface_dim.y * 0.57f) + 1.0f;
  float x = ui_camera.surface_dim.x - 26;
  size_t icon_index = (self->togglables & Status_XRadar) ? 36 : 37;

  renderer.Draw(ui_camera, Graphics::icon_sprites[icon_index], Vector2f(x, y), Layer::Gauges);

  Player* follow_player = statbox.player_manager.GetPlayerById(spectate_id);

  u32 tick = GetCurrentTick();

  if (follow_player && TICK_DIFF(GetCurrentTick(), follow_player->last_extra_timestamp) < kExtraDataTimeout) {
    char rows[6][64];

    render_extra_lines = 4;

    sprintf(rows[0], "Engy:%-5d S2CLatency:%dms", (u32)follow_player->energy, follow_player->s2c_latency * 10);
    sprintf(rows[1], "Brst:%-2d Repl:%-2d Prtl:%-2d", follow_player->bursts, follow_player->repels,
            follow_player->portals);
    sprintf(rows[2], "Decy:%-2d Thor:%-2d", follow_player->decoys, follow_player->thors);
    sprintf(rows[3], "Wall:%-2d Rckt:%-2d", follow_player->bricks, follow_player->rockets);
    sprintf(rows[4], "%6s  %s", follow_player->super ? "Super!" : "", follow_player->shields ? "Shields" : "");
    rows[5][0] = 0;
    if (follow_player->flag_timer > 0) {
      sprintf(rows[5], "Timer:%d", follow_player->flag_timer);
      ++render_extra_lines;
    }

    if (follow_player->super || follow_player->shields) {
      ++render_extra_lines;
    }

    float x = ui_camera.surface_dim.x / 2.0f;

    for (size_t i = 0; i < 6; ++i) {
      renderer.DrawText(ui_camera, rows[i], TextColor::White, Vector2f(x, (float)i * 12), Layer::Gauges);
    }
  }

  // Deviate from Continuum spectator view by showing dead player's respawn timer while spectating them.
  if (follow_player && follow_player->enter_delay > 0 &&
      !statbox.player_manager.explode_animation.IsAnimating(follow_player->explode_anim_t)) {
    char output[256];
    sprintf(output, "%.1f", follow_player->enter_delay);
    renderer.DrawText(ui_camera, output, TextColor::DarkRed, ui_camera.surface_dim * 0.5f, Layer::TopMost,
                      TextAlignment::Center);
  }
}

bool SpectateView::SpectateSelected() {
  Player* selected = statbox.GetSelectedPlayer();

  if (selected) {
    return SpectatePlayer(*selected);
  }

  return false;
}

bool SpectateView::SpectatePlayer(Player& player) {
  u32 tick = GetCurrentTick();

  if (TICK_DIFF(tick, last_spectate_packet) < 10) return false;

  if (player.ship != 8 && player.id != spectate_id) {
    spectate_id = player.id;
    spectate_frequency = player.frequency;
    last_spectate_packet = tick;

    connection.SendSpectateRequest(spectate_id);
    return true;
  }

  return false;
}

bool SpectateView::OnAction(InputAction action) {
  Player* self = statbox.player_manager.GetSelf();

  if (!self || self->ship != 8) return false;

  // TODO: F5 to follow ball and F4 to spectate multiple players
  if (action == InputAction::Bullet || action == InputAction::Bomb) {
    return SpectateSelected();
  }

  return false;
}

}  // namespace null
