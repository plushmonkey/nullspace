#include "StatBox.h"

#include <null/BannerPool.h>
#include <null/InputState.h>
#include <null/Platform.h>
#include <null/net/PacketDispatcher.h>
#include <null/render/Camera.h>
#include <null/render/Graphics.h>
#include <null/render/SpriteRenderer.h>
//
#include <stdio.h>

#include <algorithm>

namespace null {

constexpr float kBorder = 3.0f;
constexpr float kHeaderHeight = 14.0f;
constexpr float kSpectateWidth = 8.0f;

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

static void OnPlayerFrequencyChangePkt(void* user, u8* pkt, size_t size) {
  StatBox* statbox = (StatBox*)user;
  statbox->OnPlayerFrequencyChange(pkt, size);
}

static void OnPlayerBannerChangePkt(void* user, u8* pkt, size_t size) {
  StatBox* statbox = (StatBox*)user;

  statbox->TriggerRebuild();
}

StatBox::StatBox(PlayerManager& player_manager, BannerPool& banners, PacketDispatcher& dispatcher)
    : player_manager(player_manager), banners(banners) {
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerLeaving, OnPlayerLeavePkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::FrequencyChange, OnPlayerFrequencyChangePkt, this);
  dispatcher.Register(ProtocolS2C::PlayerBannerChange, OnPlayerBannerChangePkt, this);

  sliding_view.top = 0;
  sliding_view.size = 20;
}

void StatBox::TriggerRebuild() {
  this->rebuild = true;
}

void StatBox::OnAction(InputAction action, bool menu) {
  switch (action) {
    case InputAction::StatBoxPrevious: {
      if (menu) {
        sliding_view.Decrement();
      } else {
        if (view_type == StatViewType::None) {
          view_type = StatViewType::Names;
        } else if (selected_index > 0) {
          --selected_index;
        }
      }

      TriggerRebuild();
    } break;
    case InputAction::StatBoxNext: {
      if (menu) {
        sliding_view.Increment();
      } else {
        if (view_type == StatViewType::None) {
          view_type = StatViewType::Names;
        } else if (selected_index < player_manager.player_count - 1) {
          ++selected_index;
        }
      }
      TriggerRebuild();
    } break;
    case InputAction::StatBoxPreviousPage: {
      if (sliding_view.top >= sliding_view.size) {
        sliding_view.top -= sliding_view.size;
        selected_index -= sliding_view.size;
      } else {
        sliding_view.top = 0;
        selected_index = 0;
      }

      TriggerRebuild();
    } break;
    case InputAction::StatBoxNextPage: {
      size_t max_count = player_manager.player_count;

      selected_index += sliding_view.size;

      if (selected_index >= max_count) {
        selected_index = max_count - 1;
      }

      if (selected_index >= sliding_view.size) {
        sliding_view.top = selected_index - sliding_view.size;
      } else {
        sliding_view.top = 0;
      }

      TriggerRebuild();
    } break;
    case InputAction::StatBoxCycle: {
      constexpr size_t kStatBoxViewCount = 7;

      view_type = (StatViewType)(((int)view_type + 1) % kStatBoxViewCount);
      TriggerRebuild();
    } break;
    default: {
    } break;
  }
}

void StatBox::Render(Camera& camera, SpriteRenderer& renderer) {
  Player* me = player_manager.GetSelf();

  if (!me || view_type == StatViewType::None) return;

  if (rebuild) {
    rebuild = false;
    UpdateView();
  }

  // Render background
  SpriteRenderable background = Graphics::GetColor(ColorType::Background, view_dimensions);

  renderer.Draw(camera, background, Vector2f(3, 3), Layer::TopMost);

  for (size_t i = 0; i < renderable_count; ++i) {
    StatRenderableOutput* output = renderable_outputs + i;
    SpriteRenderable renderable = *output->renderable;
    renderable.dimensions = output->dimensions;

    renderer.Draw(camera, renderable, output->position, Layer::TopMost);
  }

  for (size_t i = 0; i < text_count; ++i) {
    StatTextOutput* output = text_outputs + i;

    renderer.DrawText(camera, output->text, output->color, output->position, Layer::TopMost, output->alignment);
  }

  Graphics::DrawBorder(renderer, camera, view_dimensions * 0.5f + Vector2f(kBorder, kBorder), view_dimensions * 0.5f);
}

void StatBox::UpdateSlidingView() {
  if (sliding_view.size > sliding_view.max_size) {
    sliding_view.size = sliding_view.max_size;
  }

  if (player_manager.player_count - sliding_view.top < sliding_view.size) {
    if (player_manager.player_count >= sliding_view.size) {
      sliding_view.top = player_manager.player_count - sliding_view.size;
    } else {
      sliding_view.top = 0;
    }
  }

  if (selected_index >= sliding_view.top + sliding_view.size) {
    ++sliding_view.top;
  } else if (selected_index < sliding_view.top) {
    --sliding_view.top;
  }
}

void StatBox::RecordNamesView(const Player& me) {
  constexpr float kNamesWidth = 108.0f;
  float width = kNamesWidth;

  StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
  sprintf(count_output->text, "%zd", player_manager.player_count);

  StatRenderableOutput* separator_outout =
      AddRenderableOutput(GetSeparatorRenderable(), Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  size_t i = 0;
  for (i = 0; i < sliding_view.count(); ++i) {
    size_t index = sliding_view.begin() + i;
    if (index >= player_manager.player_count) break;
    Player* player = player_manager.GetPlayerById(player_view[index]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == index, player->frequency == me.frequency);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + i * 12.0f);
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

  StatRenderableOutput* separator_outout =
      AddRenderableOutput(GetSeparatorRenderable(), Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  size_t i = 0;
  for (i = 0; i < sliding_view.count(); ++i) {
    size_t index = sliding_view.begin() + i;
    if (index >= player_manager.player_count) break;
    Player* player = player_manager.GetPlayerById(player_view[index]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == index, player->frequency == me.frequency);

    BannerRegistration* banner = banners.GetRegistration(player->id);

    if (banner) {
      float x = kBorder + kSpectateWidth + 3.0f + 12 * 8;

      AddRenderableOutput(&banner->renderable, Vector2f(x, y + 1.0f), Vector2f(12, 8));
    }

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* points_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", player->flag_points + player->kill_points);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + i * 12.0f);
}

inline int GetFrequencyCount(PlayerManager& player_manager, int frequency) {
  int count = 0;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->frequency == frequency) {
      ++count;
    }
  }

  return count;
}

void StatBox::RecordTeamSortView(const Player& me) {
  constexpr float kBaseWidth = 124;
  float width = kBaseWidth + GetPointsWidth();

  // Output header and separator
  {
    StatTextOutput* count_output = AddTextOutput(Vector2f(49, kBorder + 1), TextColor::Green, TextAlignment::Center);
    sprintf(count_output->text, "%zd", player_manager.player_count);

    StatTextOutput* header_sort_output =
        AddTextOutput(Vector2f(width, kBorder + 1), TextColor::Green, TextAlignment::Right);
    sprintf(header_sort_output->text, "Team Sort");

    StatRenderableOutput* separator_outout =
        AddRenderableOutput(GetSeparatorRenderable(), Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));
  }

  // Start right after the header
  float y = kBorder + kHeaderHeight + 1.0f;

  bool starting_freq_output = true;

  // Set the starting frequency as the first player's frequency being output
  s32 previous_freq = player_manager.GetPlayerById(player_view[sliding_view.begin()])->frequency;

  // Frequency should only be output if the first player in the sliding window is on a new frequency
  if (sliding_view.begin() > 0) {
    size_t previous_index = sliding_view.begin() - 1;
    u32 prev_freq = player_manager.GetPlayerById(player_view[previous_index])->frequency;
    u32 current_freq = player_manager.GetPlayerById(player_view[sliding_view.begin()])->frequency;

    starting_freq_output = prev_freq != current_freq;
  }

  size_t lines = 0;

  for (size_t i = 0; i < sliding_view.count() && lines < sliding_view.count(); ++i) {
    size_t index = sliding_view.begin() + i;

    if (index >= player_manager.player_count) break;

    u16 player_id = player_view[index];
    Player* player = player_manager.GetPlayerById(player_id);

    if (player->frequency != previous_freq || (i == 0 && starting_freq_output)) {
      int freq_count = GetFrequencyCount(player_manager, player->frequency);

      StatTextOutput* freq_output = AddTextOutput(Vector2f(kBorder + 1, y), TextColor::DarkRed, TextAlignment::Left);
      sprintf(freq_output->text, "%.4d-------------", player->frequency);

      StatTextOutput* freqcount_output =
          AddTextOutput(Vector2f(kBorder + 1 + 18 * 8, y), TextColor::DarkRed, TextAlignment::Left);
      sprintf(freqcount_output->text, "%d", freq_count);

      previous_freq = player->frequency;
      y += 12.0f;

      if (++lines >= sliding_view.count() && i != 0 && index != selected_index) {
        break;
      }
    }

    RecordName(player, y, selected_index == index, player->frequency == me.frequency);

    BannerRegistration* banner = banners.GetRegistration(player->id);

    if (banner) {
      float x = kBorder + kSpectateWidth + 3.0f + 12 * 8;

      AddRenderableOutput(&banner->renderable, Vector2f(x, y + 1.0f), Vector2f(12, 8));
    }

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* points_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(points_output->text, "%d", player->flag_points + player->kill_points);

    y += 12.0f;
  }

  view_dimensions = Vector2f(width, y - 3.0f);
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

  StatRenderableOutput* separator_outout =
      AddRenderableOutput(GetSeparatorRenderable(), Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

  size_t i = 0;
  for (i = 0; i < sliding_view.count(); ++i) {
    size_t index = sliding_view.begin() + i;

    if (index >= player_manager.player_count) break;

    Player* player = player_manager.GetPlayerById(player_view[index]);

    float y = kBorder + kHeaderHeight + 1.0f + i * 12.0f;
    RecordName(player, y, selected_index == index, player->frequency == me.frequency);

    BannerRegistration* banner = banners.GetRegistration(player->id);

    if (banner) {
      float x = kBorder + kSpectateWidth + 3.0f + 12 * 8;

      AddRenderableOutput(&banner->renderable, Vector2f(x, y + 1.0f), Vector2f(12, 8));
    }

    TextColor color = player->frequency == me.frequency ? TextColor::Yellow : TextColor::White;

    StatTextOutput* squad_output = AddTextOutput(Vector2f(kSquadX, y), color, TextAlignment::Left);
    sprintf(squad_output->text, "%.10s", player->squad);

    StatTextOutput* w_output = AddTextOutput(Vector2f(kWX + 8, y), color, TextAlignment::Right);
    sprintf(w_output->text, "%d", player->wins);

    StatTextOutput* l_output = AddTextOutput(Vector2f(kLX + 8, y), color, TextAlignment::Right);
    sprintf(l_output->text, "%d", player->losses);

    float ave = 0.0f;

    s32 r = ((player->kill_points + (player->wins - player->losses) * 10) * 10) / (player->wins + 100);

    if (r < 0) {
      r = 0;
    }

    if (player->wins > 0) {
      ave = player->kill_points / (float)player->wins;
    }

    StatTextOutput* r_output = AddTextOutput(Vector2f(kRX + 8, y), color, TextAlignment::Right);
    sprintf(r_output->text, "%d", r);

    StatTextOutput* ave_output = AddTextOutput(Vector2f(width, y), color, TextAlignment::Right);
    sprintf(ave_output->text, "%.1f", ave);
  }

  view_dimensions = Vector2f(width, kHeaderHeight + 1.0f + i * 12.0f);
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

  StatRenderableOutput* separator_outout =
      AddRenderableOutput(GetSeparatorRenderable(), Vector2f(kBorder, kBorder + 13), Vector2f(width, 1));

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
      AddRenderableOutput(&Graphics::spectate_sprites[0], Vector2f(kBorder, y + 3),
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
      AddRenderableOutput(&Graphics::spectate_sprites[0], Vector2f(kBorder, y + 3),
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
    AddRenderableOutput(&Graphics::spectate_sprites[spec_index], Vector2f(kBorder, y + 3.0f),
                        Graphics::spectate_sprites[spec_index].dimensions);
  }

  TextColor color = same_freq ? TextColor::Yellow : TextColor::White;
  StatTextOutput* output = AddTextOutput(Vector2f(kBorder + kSpectateWidth + 2.0f, y), color, TextAlignment::Left);

  sprintf(output->text, "%.12s", player->name);
}

void StatBox::OnPlayerEnter(u8* pkt, size_t size) {
  TriggerRebuild();
}

void StatBox::OnPlayerLeave(u8* pkt, size_t size) {
  TriggerRebuild();
}

void StatBox::OnPlayerFreqAndShipChange(u8* pkt, size_t size) {
  TriggerRebuild();
}

void StatBox::OnPlayerFrequencyChange(u8* pkt, size_t size) {
  TriggerRebuild();
}

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

  UpdateSlidingView();
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
    case StatViewType::Frequency:
    case StatViewType::TeamSort: {
      SortByFreq(*self);
    } break;
    default: {
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

StatRenderableOutput* StatBox::AddRenderableOutput(SpriteRenderable* renderable, const Vector2f& position,
                                                   const Vector2f& dimensions) {
  StatRenderableOutput* output = renderable_outputs + renderable_count++;

  output->renderable = renderable;
  output->position = position;
  output->dimensions = dimensions;

  return output;
}

SpriteRenderable* StatBox::GetSeparatorRenderable() {
  // Store the renderable in the object so it has a static address for StatRenderableOutput iteration.
  separator_renderable = Graphics::colors.GetRenderable(ColorType::Border1);

  return &separator_renderable;
}

}  // namespace null
