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
    RecordView();
  } else if (codepoint == NULLSPACE_KEY_PAGE_DOWN && selected_index < player_manager.player_count - 1) {
    if (shift) {
      // TODO: Full page skips instead of directly to the end
      selected_index = player_manager.player_count - 1;
    } else {
      ++selected_index;
    }
    RecordView();
  } else if (codepoint == NULLSPACE_KEY_F2) {
    view_type = (StatViewType)(((int)view_type + 1) % 7);
    UpdateView();
  }
}

void StatBox::Render(Camera& camera, SpriteRenderer& renderer) {
  Player* me = player_manager.GetSelf();

  if (!me || view_type == StatViewType::None) return;

  // Render background
  SpriteRenderable background = Graphics::color_sprites[kBackgroundColorIndex];
  background.dimensions = view_dimensions;

  renderer.Draw(camera, background, Vector2f(3, 3));

  for (size_t i = 0; i < renderable_count; ++i) {
    StatRenderableOutput* output = renderable_outputs + i;
    SpriteRenderable renderable = *output->renderable;
    renderable.dimensions = output->dimensions;

    renderer.Draw(camera, renderable, output->position);
  }

  for (size_t i = 0; i < text_count; ++i) {
    StatTextOutput* output = text_outputs + i;

    renderer.DrawText(camera, output->text, output->color, output->position, output->alignment);
  }

  Graphics::DrawBorder(renderer, camera, view_dimensions * 0.5f + Vector2f(kBorder, kBorder), view_dimensions * 0.5f);
}

void StatBox::RecordNamesView(const Player& me) {
  constexpr float kNamesWidth = 108.0f;
  float width = kNamesWidth;

  StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
  sprintf(count_output->text, "%zd", player_manager.player_count);

  StatRenderableOutput* separator_outout = AddRenderableOutput(Graphics::color_sprites[kSeparatorColorIndex],
                                                               Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.GetPlayerById(player_view[i]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == i, player->frequency == me.frequency);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + player_manager.player_count * 12.0f);
}

void StatBox::RecordPointsView(const Player& me) {
  constexpr float kBaseWidth = 124;
  float width = kBaseWidth + GetPointsWidth();

  StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
  sprintf(count_output->text, "%zd", player_manager.player_count);

  StatTextOutput* header_points_output =
      AddTextOutput(Vector2f(width, kBorder + 1), TextColor::Green, TextAlignment::Right);

  if (view_type == StatViewType::PointSort) {
    sprintf(header_points_output->text, "Point Sort");
  } else {
    sprintf(header_points_output->text, "Points");
  }

  StatRenderableOutput* separator_outout = AddRenderableOutput(Graphics::color_sprites[kSeparatorColorIndex],
                                                               Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.GetPlayerById(player_view[i]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == i, player->frequency == me.frequency);

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* points_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", player->flag_points + player->kill_points);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + player_manager.player_count * 12.0f);
}

void StatBox::RecordTeamSortView(const Player& me) {
  constexpr float kBaseWidth = 124;
  float width = kBaseWidth + GetPointsWidth();

  StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
  sprintf(count_output->text, "%zd", player_manager.player_count);

  StatTextOutput* header_sort_output =
      AddTextOutput(Vector2f(width, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_sort_output->text, "Team Sort");

  StatRenderableOutput* separator_outout = AddRenderableOutput(Graphics::color_sprites[kSeparatorColorIndex],
                                                               Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  float y = kBorder + kHeaderHeight + 1.0f;
  s32 previous_freq = player_manager.GetPlayerById(player_view[0])->frequency;
  float freq_output_y = y;
  int freq_count = 0;

  y += 12.0f;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    u16 player_id = player_view[i];
    Player* player = player_manager.GetPlayerById(player_id);

    if (player->frequency != previous_freq) {
      StatTextOutput* freq_output =
          AddTextOutput(Vector2f(kBorder + 1, freq_output_y), TextColor::DarkRed, TextAlignment::Left);
      sprintf(freq_output->text, "%.4d-------------", previous_freq);

      StatTextOutput* freqcount_output =
          AddTextOutput(Vector2f(kBorder + 1 + 18 * 8, freq_output_y), TextColor::DarkRed, TextAlignment::Left);
      sprintf(freqcount_output->text, "%d", freq_count);

      freq_count = 0;
      previous_freq = player->frequency;
      freq_output_y = y;
      y += 12.0f;
    }

    ++freq_count;

    RecordName(player, y, selected_index == i, player->frequency == me.frequency);

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* points_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", player->flag_points + player->kill_points);

    y += 12.0f;
  }

  StatTextOutput* freq_output =
      AddTextOutput(Vector2f(kBorder + 1, freq_output_y), TextColor::DarkRed, TextAlignment::Left);
  sprintf(freq_output->text, "%.4d-------------", previous_freq);

  StatTextOutput* freqcount_output =
      AddTextOutput(Vector2f(kBorder + 1 + 18 * 8, freq_output_y), TextColor::DarkRed, TextAlignment::Left);
  sprintf(freqcount_output->text, "%d", freq_count);

  view_dimensions = Vector2f(width, y - 4.0f);
}

void StatBox::RecordFullView(const Player& me) {
  constexpr float kBaseWidth = 452;
  float width = kBaseWidth;

  constexpr float kSquadX = 133.0f;

  constexpr float kWX = kSquadX + (10 + 7) * 8;
  constexpr float kLX = kWX + 7 * 8;
  constexpr float kRX = kLX + 7 * 8;

  StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
  sprintf(count_output->text, "%zd", player_manager.player_count);

  StatTextOutput* header_squad_output =
      AddTextOutput(Vector2f(kSquadX, kBorder + 1), TextColor::Green, TextAlignment::Left);
  sprintf(header_squad_output->text, "Squad");

  StatTextOutput* header_w_output = AddTextOutput(Vector2f(kWX, kBorder + 1), TextColor::Green, TextAlignment::Left);
  sprintf(header_w_output->text, "W");

  StatTextOutput* header_l_output = AddTextOutput(Vector2f(kLX, kBorder + 1), TextColor::Green, TextAlignment::Left);
  sprintf(header_l_output->text, "L");

  StatTextOutput* header_r_output = AddTextOutput(Vector2f(kRX, kBorder + 1), TextColor::Green, TextAlignment::Left);
  sprintf(header_r_output->text, "R");

  StatTextOutput* header_ave_output =
      AddTextOutput(Vector2f(width, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_ave_output->text, "Ave");

  StatRenderableOutput* separator_outout = AddRenderableOutput(Graphics::color_sprites[kSeparatorColorIndex],
                                                               Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.GetPlayerById(player_view[i]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == i, player->frequency == me.frequency);

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* squad_output = AddTextOutput(Vector2f(kSquadX, y), color, TextAlignment::Left);
    sprintf(squad_output->text, "%.10s", player->squad);

    StatTextOutput* w_output = AddTextOutput(Vector2f(kWX + 8, y), color, TextAlignment::Right);
    sprintf(w_output->text, "%d", player->wins);

    StatTextOutput* l_output = AddTextOutput(Vector2f(kLX + 8, y), color, TextAlignment::Right);
    sprintf(l_output->text, "%d", player->losses);

    // TODO: Calculate stats
    float r = 0.0f;
    float ave = 0.0f;

    if (player->wins > 0) {
      ave = player->kill_points / (float)player->wins;
    }

    StatTextOutput* r_output = AddTextOutput(Vector2f(kRX + 8, y), color, TextAlignment::Right);
    sprintf(r_output->text, "%d", (int)r);

    StatTextOutput* ave_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(ave_output->text, "%.1f", ave);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + player_manager.player_count * 12.0f);
}

void StatBox::RecordFrequencyView(const Player& me) {
  constexpr float kBaseWidth = 252.0f;
  float points_width = GetPointsSumWidth();
  float width = kBaseWidth + points_width;

  float freq_x = 41 + kBorder;
  float points_x = freq_x + points_width + 33;
  float win_x = points_x + 64;
  float lose_x = win_x + 64;

  StatTextOutput* header_freq_output =
      AddTextOutput(Vector2f(freq_x, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_freq_output->text, "Freq");

  StatTextOutput* header_points_output =
      AddTextOutput(Vector2f(points_x, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_points_output->text, "Points");

  StatTextOutput* header_win_output =
      AddTextOutput(Vector2f(win_x, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_win_output->text, "Win");

  StatTextOutput* header_lose_output =
      AddTextOutput(Vector2f(lose_x, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_lose_output->text, "Lose");

  StatTextOutput* header_flag_output =
      AddTextOutput(Vector2f(width, kBorder + 1), TextColor::Green, TextAlignment::Right);
  sprintf(header_flag_output->text, "Flag");

  StatRenderableOutput* separator_outout = AddRenderableOutput(Graphics::color_sprites[kSeparatorColorIndex],
                                                               Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  Player* selected_player = GetSelectedPlayer();
  int selected_freq = 0;

  if (selected_player) {
    selected_freq = selected_player->frequency;
  }

  int freq_count = 1;
  int rendered_count = 0;

  u32 point_count = 0;
  u32 win_count = 0;
  u32 lose_count = 0;
  u32 flag_count = 0;
  u32 last_freq = player_manager.GetPlayerById(player_view[0])->frequency;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.GetPlayerById(player_view[i]);
    float y = kBorder + kHeaderHeight + 1.0f + rendered_count * 12.0f;
    bool last_output = i == player_manager.player_count - 1;

    if (player->frequency == last_freq) {
      point_count += player->kill_points + player->flag_points;
      win_count += player->wins;
      lose_count += player->losses;
      flag_count += player->flags;

      continue;
    }

    TextColor color = last_freq == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* freq_output = AddTextOutput(Vector2f(freq_x, y), color, TextAlignment::Right);
    sprintf(freq_output->text, "%4d", last_freq);

    StatTextOutput* points_output = AddTextOutput(Vector2f(points_x, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", point_count);

    StatTextOutput* win_output = AddTextOutput(Vector2f(win_x, y), color, TextAlignment::Right);
    sprintf(win_output->text, "%d", win_count);

    StatTextOutput* loss_output = AddTextOutput(Vector2f(lose_x, y), color, TextAlignment::Right);
    sprintf(loss_output->text, "%d", lose_count);

    StatTextOutput* flag_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(flag_output->text, "%d", flag_count);

    if (last_freq == selected_freq) {
      AddRenderableOutput(Graphics::spectate_sprites[0], Vector2f(kBorder, y + 3),
                          Graphics::spectate_sprites[0].dimensions);
    }

    if (last_output) {
      point_count = player->kill_points + player->flag_points;
      win_count = player->wins;
      lose_count = player->losses;
      flag_count = player->flags;
    } else {
      point_count = 0;
      win_count = 0;
      lose_count = 0;
      flag_count = 0;
    }

    last_freq = player->frequency;

    ++freq_count;
    ++rendered_count;
  }

  if (rendered_count != freq_count) {
    float y = kBorder + kHeaderHeight + 1.0f + rendered_count * 12.0f;

    TextColor color = last_freq == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* freq_output = AddTextOutput(Vector2f(freq_x, y), color, TextAlignment::Right);
    sprintf(freq_output->text, "%4d", last_freq);

    StatTextOutput* points_output = AddTextOutput(Vector2f(points_x, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", point_count);

    StatTextOutput* win_output = AddTextOutput(Vector2f(win_x, y), color, TextAlignment::Right);
    sprintf(win_output->text, "%d", win_count);

    StatTextOutput* loss_output = AddTextOutput(Vector2f(lose_x, y), color, TextAlignment::Right);
    sprintf(loss_output->text, "%d", lose_count);

    StatTextOutput* flag_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(flag_output->text, "%d", flag_count);

    if (last_freq == selected_freq) {
      AddRenderableOutput(Graphics::spectate_sprites[0], Vector2f(kBorder, y + 3),
                          Graphics::spectate_sprites[0].dimensions);
    }
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + freq_count * 12.0f);
}

void StatBox::RecordName(Player* player, float y, bool selected, bool same_freq) {
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
    AddRenderableOutput(Graphics::spectate_sprites[spec_index], Vector2f(kBorder, y + 3.0f),
                        Graphics::spectate_sprites[spec_index].dimensions);
  }

  TextColor color = same_freq ? TextColor::Yellow : TextColor::White;
  StatTextOutput* output = AddTextOutput(Vector2f(kBorder + kSpectateWidth + 2.0f, y), color, TextAlignment::Left);

  sprintf(output->text, "%.12s", player->name);
}

void StatBox::OnPlayerEnter(u8* pkt, size_t size) { UpdateView(); }

void StatBox::OnPlayerLeave(u8* pkt, size_t size) { UpdateView(); }

void StatBox::OnPlayerFreqAndShipChange(u8* pkt, size_t size) { UpdateView(); }

Player* StatBox::GetSelectedPlayer() {
  u16 selected_id = player_view[selected_index];
  return player_manager.GetPlayerById(selected_id);
}

float StatBox::GetPointsWidth() {
  u32 highest_points = 0;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;
    u32 points = player->flag_points + player->kill_points;

    if (points > highest_points) {
      highest_points = points;
    }
  }

  int digits = 0;
  do {
    highest_points /= 10;
    ++digits;
  } while (highest_points > 0);

  if (digits < 6) {
    digits = 6;
  }

  return digits * 8.0f;
}

float StatBox::GetPointsSumWidth() {
  u32 points_sum = 0;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;
    u32 points = player->flag_points + player->kill_points;

    points_sum += points;
  }

  int digits = 0;
  do {
    points_sum /= 10;
    ++digits;
  } while (points_sum > 0);

  if (digits < 6) {
    digits = 6;
  }

  return digits * 8.0f;
}

void StatBox::RecordView() {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  text_count = 0;
  renderable_count = 0;
  view_dimensions = Vector2f(0, 0);

  switch (view_type) {
    case StatViewType::Names: {
      RecordNamesView(*self);
    } break;
    case StatViewType::Points: {
      RecordPointsView(*self);
    } break;
    case StatViewType::PointSort: {
      RecordPointsView(*self);
    } break;
    case StatViewType::TeamSort: {
      RecordTeamSortView(*self);
    } break;
    case StatViewType::Full: {
      RecordFullView(*self);
    } break;
    case StatViewType::Frequency: {
      RecordFrequencyView(*self);
    } break;
    default: {
    } break;
  }
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

  RecordView();
}

// TODO: Other sort methods
void StatBox::SortView() {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  switch (view_type) {
    case StatViewType::Full:
    case StatViewType::Points:
    case StatViewType::Names: {
      SortByName(*self);
    } break;
    case StatViewType::PointSort: {
      SortByPoints(*self);
    } break;
    case StatViewType::TeamSort: {
      SortByFreq(*self);
    } break;
  }
}

void StatBox::SortByName(const Player& self) {
  size_t count = player_manager.player_count;

  size_t index = 0;
  player_view[index++] = player_manager.player_id;

  // Copy in everyone on self frequency
  for (size_t i = 0; i < count; ++i) {
    Player* player = player_manager.players + i;

    if (player->id == player_manager.player_id) continue;
    if (player->frequency != self.frequency) continue;

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

    if (player->frequency == self.frequency) continue;

    player_view[index++] = player->id;
  }

  std::sort(player_view + other_start, player_view + index, [&](u16 left, u16 right) {
    Player* lplayer = player_manager.GetPlayerById(left);
    Player* rplayer = player_manager.GetPlayerById(right);

    return null_stricmp(lplayer->name, rplayer->name) < 0;
  });
}

void StatBox::SortByPoints(const Player& self) {
  size_t count = player_manager.player_count;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    player_view[i] = player->id;
  }

  std::sort(player_view, player_view + count, [&](u16 left, u16 right) {
    Player* lplayer = player_manager.GetPlayerById(left);
    Player* rplayer = player_manager.GetPlayerById(right);

    s32 lpoints = lplayer->flag_points + lplayer->kill_points;
    s32 rpoints = rplayer->flag_points + rplayer->kill_points;

    return lpoints > rpoints;
  });
}

void StatBox::SortByFreq(const Player& self) {
  size_t count = player_manager.player_count;

  size_t index = 0;
  player_view[index++] = player_manager.player_id;

  // Copy in everyone on self frequency
  for (size_t i = 0; i < count; ++i) {
    Player* player = player_manager.players + i;

    if (player->id == player_manager.player_id) continue;
    if (player->frequency != self.frequency) continue;

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

    if (player->frequency == self.frequency) continue;

    player_view[index++] = player->id;
  }

  std::sort(player_view + other_start, player_view + index, [&](u16 left, u16 right) {
    Player* lplayer = player_manager.GetPlayerById(left);
    Player* rplayer = player_manager.GetPlayerById(right);

    return lplayer->frequency < rplayer->frequency ||
           (lplayer->frequency == rplayer->frequency && null_stricmp(lplayer->name, rplayer->name) < 0);
  });
}

StatTextOutput* StatBox::AddTextOutput(const Vector2f& position, TextColor color, TextAlignment alignment) {
  StatTextOutput* output = text_outputs + text_count++;

  output->position = position;
  output->color = color;
  output->alignment = alignment;

  return output;
}

StatRenderableOutput* StatBox::AddRenderableOutput(SpriteRenderable& renderable, const Vector2f& position,
                                                   const Vector2f& dimensions) {
  StatRenderableOutput* output = renderable_outputs + renderable_count++;

  output->renderable = &renderable;
  output->position = position;
  output->dimensions = dimensions;

  return output;
}

}  // namespace null
