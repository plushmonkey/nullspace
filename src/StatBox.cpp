#include "StatBox.h"

#include <algorithm>
#include <cstdio>

#include "InputState.h"
#include "Platform.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/Graphics.h"
#include "render/SpriteRenderer.h"

namespace null {

constexpr float kBorder = 3.0f;
constexpr float kHeaderHeight = 14.0f;
constexpr float kSpectateWidth = 8.0f;

constexpr size_t kSeparatorColorIndex = 1;
constexpr size_t kBackgroundColorIndex = 16;

constexpr float kViewWidth[] = {108, 172, 172, 172, 380, 268};

static void OnPlayerEnterPkt(void* user, u8* pkt, size_t size) {
  StatBox* statbox = (StatBox*)user;
  statbox->OnPlayerEnter(pkt, size);
}

static void OnPlayerLeavePkt(void* user, u8* pkt, size_t size) {
  StatBox* statbox = (StatBox*)user;
  statbox->OnPlayerLeave(pkt, size);
}

static void OnPlayerFreqAndShipChangePkt(void* user, u8* pkt, size_t size) {
  StatBox* statbox = (StatBox*)user;
  statbox->OnPlayerFreqAndShipChange(pkt, size);
}

StatBox::StatBox(PlayerManager& player_manager, PacketDispatcher& dispatcher) : player_manager(player_manager) {
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerLeaving, OnPlayerLeavePkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
}

void StatBox::OnCharacterPress(int codepoint, int mods) {
  bool shift = mods & NULLSPACE_KEY_MOD_SHIFT;

  if (codepoint == NULLSPACE_KEY_PAGE_UP && selected_index > 0) {
    if (shift) {
      selected_index = 0;
    } else {
      --selected_index;
    }
  } else if (codepoint == NULLSPACE_KEY_PAGE_DOWN && selected_index < player_manager.player_count - 1) {
    if (shift) {
      // TODO: Full page skips instead of directly to the end
      selected_index = player_manager.player_count - 1;
    } else {
      ++selected_index;
    }
  }
}

void StatBox::Render(Camera& camera, SpriteRenderer& renderer) {
  Player* me = player_manager.GetSelf();

  if (!me) return;

  float box_height = kHeaderHeight + player_manager.player_count * 12.0f;
  Vector2f dimensions(kViewWidth[(size_t)view_type], box_height);

  // Render background
  SpriteRenderable background = Graphics::color_sprites[kBackgroundColorIndex];
  background.dimensions = dimensions;

  renderer.Draw(camera, background, Vector2f(3, 3));

  char count_text[16];
  sprintf(count_text, "%zd", player_manager.player_count);

  renderer.DrawText(camera, count_text, TextColor::Green, Vector2f(dimensions.x * 0.5f - 5.0f, kBorder + 1),
                    TextAlignment::Center);

  // Render header separator
  SpriteRenderable separator = Graphics::color_sprites[kSeparatorColorIndex];
  separator.dimensions = Vector2f(dimensions.x, 1);

  renderer.Draw(camera, separator, Vector2f(kBorder, kBorder + 13));

  Graphics::DrawBorder(renderer, camera, dimensions * 0.5f + Vector2f(kBorder, kBorder), dimensions * 0.5f);

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    u16 player_id = player_view[i];

    Player* player = player_manager.GetPlayerById(player_id);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RenderName(camera, renderer, player, y, selected_index == i, player->frequency == me->frequency);
  }
}

void StatBox::RenderName(Camera& camera, SpriteRenderer& renderer, Player* player, float y, bool selected,
                         bool same_freq) {
  size_t spec_index = -1;

  if (selected) {
    if (player->ship == 8) {
      spec_index = 2;
    } else {
      spec_index = 0;
    }
  } else if (player->ship == 8) {
    spec_index = 1;
  }

  if (spec_index != -1) {
    renderer.Draw(camera, Graphics::spectate_sprites[spec_index], Vector2f(kBorder, y + 3.0f));
  }

  char name[20];
  sprintf(name, "%.12s", player->name);

  TextColor color = same_freq ? TextColor::Yellow : TextColor::White;
  renderer.DrawText(camera, name, color, Vector2f(kBorder + kSpectateWidth + 2.0f, y));
}

void StatBox::OnPlayerEnter(u8* pkt, size_t size) { UpdateView(); }

void StatBox::OnPlayerLeave(u8* pkt, size_t size) { UpdateView(); }

void StatBox::OnPlayerFreqAndShipChange(u8* pkt, size_t size) { UpdateView(); }

Player* StatBox::GetSelectedPlayer() {
  u16 selected_id = player_view[selected_index];
  return player_manager.GetPlayerById(selected_id);
}

void StatBox::UpdateView() {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  u16 selected_id = 0;

  if (selected_index == -1) {
    selected_id = self->id;
  } else {
    selected_id = player_view[selected_index];
  }

  SortView();

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    if (player_view[i] == selected_id) {
      selected_index = i;
      break;
    }
  }

  if (selected_index >= player_manager.player_count) {
    selected_index = player_manager.player_count - 1;
  }
}

// TODO: Other sort methods
void StatBox::SortView() {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  size_t count = player_manager.player_count;

  size_t index = 0;
  player_view[index++] = player_manager.player_id;

  // Copy in everyone on self frequency
  for (size_t i = 0; i < count; ++i) {
    Player* player = player_manager.players + i;

    if (player->id == player_manager.player_id) continue;
    if (player->frequency != self->frequency) continue;

    player_view[index++] = player->id;
  }

  // Sort own frequency
  std::sort(player_view + 1, player_view + index, [&](u16 left, u16 right) {
    Player* lplayer = player_manager.GetPlayerById(left);
    Player* rplayer = player_manager.GetPlayerById(right);

    return null_stricmp(lplayer->name, rplayer->name) < 0;
  });

  size_t other_start = index;

  // Copy in everyone not on self frequency
  for (size_t i = 0; i < count; ++i) {
    Player* player = player_manager.players + i;

    if (player->frequency == self->frequency) continue;

    player_view[index++] = player->id;
  }

  std::sort(player_view + other_start, player_view + index, [&](u16 left, u16 right) {
    Player* lplayer = player_manager.GetPlayerById(left);
    Player* rplayer = player_manager.GetPlayerById(right);

    return null_stricmp(lplayer->name, rplayer->name) < 0;
  });
}

}  // namespace null
