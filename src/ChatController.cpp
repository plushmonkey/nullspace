#include "ChatController.h"

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Clock.h"
#include "Platform.h"
#include "PlayerManager.h"
#include "ShipController.h"
#include "StatBox.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"

namespace null {

constexpr float kFontWidth = 8.0f;
constexpr float kFontHeight = 12.0f;

struct ChatSpan {
  const char* begin;
  const char* end;
};

void WrapChat(const char* mesg, s32 linesize, ChatSpan* lines, size_t* linecount, bool skip_spaces = true) {
  // Trim front
  while (skip_spaces && *mesg && *mesg == ' ') ++mesg;

  s32 size = (s32)strlen(mesg);

  if (size < linesize) {
    lines[0].begin = mesg;
    lines[0].end = mesg + size;
    *linecount = 1;
    return;
  }

  s32 last_end = 0;
  for (int count = 1; count <= 16; ++count) {
    s32 end = last_end + linesize;

    if (end >= size) {
      end = size;
      lines[count - 1].begin = mesg + last_end;
      lines[count - 1].end = mesg + end;
      *linecount = count;
      break;
    }

    if (skip_spaces) {
      if (mesg[end] == ' ') {
        // Go backwards to trim off last space
        for (; end >= 0; --end) {
          if (mesg[end] != ' ') {
            ++end;
            break;
          }
        }
      } else {
        for (; end >= 0; --end) {
          // Go backwards looking for a space
          if (mesg[end] == ' ') {
            break;
          }
        }
      }
    }

    if (end <= last_end) {
      end = last_end + linesize;
    }

    lines[count - 1].begin = mesg + last_end;
    lines[count - 1].end = mesg + end;

    last_end = end;
    *linecount = count;

    // Trim again for next line
    while (skip_spaces && last_end < size && mesg[last_end] == ' ') {
      ++last_end;
    }
  }
}

static void OnChatPacketRaw(void* user, u8* packet, size_t size) {
  ChatController* controller = (ChatController*)user;

  controller->OnChatPacket(packet, size);
}

ChatController::ChatController(PacketDispatcher& dispatcher, Connection& connection, PlayerManager& player_manager,
                               StatBox& statbox)
    : connection(connection), player_manager(player_manager), statbox(statbox) {
  dispatcher.Register(ProtocolS2C::Chat, OnChatPacketRaw, this);
}

void ChatController::SendInput() {
  if (input[0] == 0) return;

  Player* player = player_manager.GetSelf();
  if (player == nullptr) return;

  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);

  ChatType type = GetInputType();

  u16 target = 0;
  char* mesg = input;
  int channel = 1;

  while (*mesg && *mesg == ' ') ++mesg;

  if (HandleInputCommands()) {
    input[0] = 0;
    return;
  }

  if (type == ChatType::Private) {
    if (mesg[0] == '/') {
      ++mesg;

      Player* selected = statbox.GetSelectedPlayer();

      if (selected) {
        target = selected->id;
      }
    } else if (mesg[0] == ':') {
      char* original_mesg = mesg;
      char* target_name = mesg + 1;

      while (*mesg++) {
        if (*mesg == ':') {
          size_t name_len = mesg - target_name;
          ++mesg;

          Player* best = GetBestPlayerNameMatch(target_name, name_len);

          if (!best) {
            type = ChatType::RemotePrivate;
            mesg = original_mesg;
          } else {
            target = best->id;
          }
          break;
        }
      }
    }
  } else if (type == ChatType::OtherTeam) {
    Player* selected = statbox.GetSelectedPlayer();

    if (selected) {
      target = selected->id;
    }

    ++mesg;
  } else if (type == ChatType::Channel) {
    ++mesg;
  } else if (type == ChatType::Team) {
    ++mesg;
    if (input[0] == '/' && input[1] == '/') {
      ++mesg;
    }
  }

  size_t size = strlen(mesg) + 1;

  buffer.WriteU8(0x06);
  buffer.WriteU8((u8)type);
  buffer.WriteU8(0x00);  // Sound
  buffer.WriteU16(target);
  buffer.WriteString(mesg, size);

  connection.packet_sequencer.SendReliableMessage(connection, buffer.data, buffer.GetSize());

  if (type == ChatType::Channel) {
    char channel_output[250];

    char* ptr = mesg;

    while (*ptr++) {
      if (*ptr == ';') {
        channel = atoi(mesg);
        mesg = ptr + 1;
        break;
      }
    }

    sprintf(channel_output, "%d:%s> %s", channel, player->name, mesg);

    PushEntry(channel_output, strlen(channel_output), type);
  } else {
    ChatEntry* entry = PushEntry(mesg, size, type);

    memcpy(entry->sender, player->name, 20);
  }

  input[0] = 0;
}

Player* ChatController::GetBestPlayerNameMatch(char* name, size_t length) {
  Player* best_match = nullptr;

  // Loop through each player looking at the first 'length' characters of their name.
  // If they match up to the length then add them as a candidate.
  // If they match up to the length and the name is exactly the same length as the check name then return that one.
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* p = player_manager.players + i;

    bool is_match = true;

    for (size_t j = 0; j < NULLSPACE_ARRAY_SIZE(p->name) && j < length; ++j) {
      char p_curr = tolower(p->name[j]);
      char n_curr = tolower(name[j]);

      if (p_curr != n_curr) {
        is_match = false;
        break;
      }
    }

    if (is_match) {
      best_match = p;

      // If they match up until the length of the check name and they are the same length then it must be exact
      if (strlen(p->name) == length) {
        return p;
      }
    }
  }

  return best_match;
}

inline int GetShipStatusPercent(u32 upgrade, u32 maximum, u32 current) {
  if (upgrade == 0) return 100;

  u32 maximum_upgrades = maximum / upgrade;
  u32 current_upgrades = current / upgrade;

  return (current_upgrades * 100) / maximum_upgrades;
}

bool ChatController::HandleInputCommands() {
  if (input[0] == '=') {
    if (input[1] >= '0' && input[1] <= '9') {
      // TODO: Check energy
      u16 freq = atoi(input + 1);

      connection.SendFrequencyChange(freq);
    }
    return true;
  } else if (input[0] == '?') {
    if (strstr(input, "?go") == input) {
      char* ptr = input + 3;
      if (*ptr == ' ') {
        ++ptr;
      } else if (*ptr != 0) {
        return false;
      }

      Player* self = player_manager.GetSelf();
      int ship = 8;

      if (self) {
        ship = self->ship;
      }

      if (*ptr != 0) {
        bool is_number = true;
        for (size_t i = 0; i < 16; ++i) {
          if (ptr[i] == 0) break;

          if (ptr[i] < '0' || ptr[i] > '9') {
            is_number = false;
            break;
          }
        }

        if (is_number) {
          int number = atoi(ptr);

          if (number > 0xFFFF) {
            number = 0xFFFF;
          }

          connection.SendArenaLogin(ship, 0, 1920, 1080, number, "");
        } else {
          connection.SendArenaLogin(ship, 0, 1920, 1080, 0xFFFD, ptr);
        }
      } else {
        connection.SendArenaLogin(ship, 0, 1920, 1080, 0xFFFF, "");
      }

      return true;
    } else if (strstr(input, "?namelen ") == input) {
      char* arg = input + 9;

      int new_len = atoi(arg);

      if (new_len > 0) {
        if (new_len > 24) {
          new_len = 24;
        }

        g_Settings.chat_namelen = new_len;
      }

      char response_mesg[256];
      sprintf(response_mesg, "Message Name Length: %d", g_Settings.chat_namelen);

      ChatEntry* entry = PushEntry(response_mesg, strlen(response_mesg), ChatType::Arena);

      return true;
    } else if (strstr(input, "?status") == input && strlen(input) == 7) {
      ShipController* ship_controller = player_manager.ship_controller;
      Player* self = player_manager.GetSelf();

      if (self && self->ship != 8 && ship_controller) {
        ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

        int recharge = GetShipStatusPercent(ship_settings.UpgradeRecharge, ship_settings.MaximumRecharge,
                                            ship_controller->ship.recharge);
        int thruster = GetShipStatusPercent(ship_settings.UpgradeThrust, ship_settings.MaximumThrust,
                                            ship_controller->ship.thrust);
        int speed =
            GetShipStatusPercent(ship_settings.UpgradeSpeed, ship_settings.MaximumSpeed, ship_controller->ship.speed);

        int rotation = GetShipStatusPercent(ship_settings.UpgradeRotation, ship_settings.MaximumRotation,
                                            ship_controller->ship.rotation);
        u32 shrapnel = ship_controller->ship.shrapnel;

        char output[256];
        sprintf(output, "Recharge:%d%%  Thruster:%d%%  Speed:%d%%  Rotation:%d%%  Shrapnel:%u", recharge, thruster,
                speed, rotation, shrapnel);

        PushEntry(output, strlen(output), ChatType::Arena);
      }

      return true;
    }
  }

  return false;
}

void ChatController::Update(float dt) {
  if (cursor.animation.sprite) {
    cursor.animation.t += dt;

    if (cursor.animation.t >= cursor.animation.GetDuration()) {
      cursor.animation.t -= cursor.animation.GetDuration();
    }
  }
}

void ChatController::Render(Camera& camera, SpriteRenderer& renderer) {
  // TODO: pull radar size from somewhere else
  float radar_size = camera.surface_dim.x * 0.165f + 11;
  float name_size = 12 * kFontWidth;
  float y = camera.surface_dim.y;
  size_t display_amount = display_full ? 64 : (size_t)((camera.surface_dim.y / 100) + 1);
  ChatSpan lines[16];
  size_t linecount;

  if (input[0] != 0) {
    TextColor colors[] = {TextColor::White, TextColor::White, TextColor::White, TextColor::Yellow, TextColor::White,
                          TextColor::Green, TextColor::White, TextColor::Green, TextColor::White,  TextColor::Red};
    ChatType type = GetInputType();
    TextColor color = colors[(size_t)type];

    u32 max_characters = (u32)((camera.surface_dim.x - radar_size) / kFontWidth);

    WrapChat(input, max_characters, lines, &linecount, false);

    y -= kFontHeight * linecount;

    char output[512];
    for (size_t i = 0; i < linecount; ++i) {
      ChatSpan* span = lines + i;
      u32 length = (u32)(span->end - span->begin);

      sprintf(output, "%.*s", length, span->begin);

      renderer.DrawText(camera, output, color, Vector2f(0, y + i * kFontHeight), Layer::Chat);

      if (i == linecount - 1 && cursor.animation.sprite) {
        float cursor_x = length * 8.0f + 1.0f;
        float cursor_y = y + i * kFontHeight;

        renderer.Draw(camera, cursor.animation.GetFrame(), Vector2f(cursor_x, cursor_y), Layer::Chat);
      }
    }

    display_amount -= linecount;
  }

  if (entry_index == 0) return;

  size_t start = entry_index;
  size_t end = entry_index - display_amount;

  if (start < display_amount) {
    end = 0;
  }

  for (size_t i = start; i > end; --i) {
    size_t index = (i - 1) % NULLSPACE_ARRAY_SIZE(entries);
    ChatEntry* entry = entries + index;

    u32 max_characters = (u32)((camera.surface_dim.x - radar_size - name_size) / kFontWidth);

    if (entry->type == ChatType::Arena || entry->type == ChatType::RedWarning || entry->type == ChatType::RedError ||
        entry->type == ChatType::Channel) {
      max_characters = (u32)((camera.surface_dim.x - radar_size) / kFontWidth);
    }

    WrapChat(entry->message, max_characters, lines, &linecount);

    y -= kFontHeight * linecount;

    switch (entry->type) {
      case ChatType::RemotePrivate:
      case ChatType::Fuchsia:
      case ChatType::Arena: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          TextColor color = entry->type == ChatType::Fuchsia ? TextColor::Fuschia : TextColor::Green;

          renderer.DrawText(camera, output, color, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::PublicMacro:
      case ChatType::Public: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Blue, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Team: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Yellow, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::OtherTeam: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> ", spaces, "", g_Settings.chat_namelen, entry->sender);
          float skip = strlen(output) * kFontWidth;
          renderer.DrawText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight), Layer::Chat);

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::Blue, Vector2f(skip, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Private: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::RedWarning:
      case ChatType::RedError: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::DarkRed, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Channel: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::Red, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      default: {
      } break;
    }
  }
}

void ChatController::OnCharacterPress(int codepoint, int mods) {
  bool control = mods & NULLSPACE_KEY_MOD_CONTROL;

  if (codepoint == NULLSPACE_KEY_BACKSPACE) {
    size_t size = strlen(input);

    if (control) {
      input[0] = 0;
    } else if (size > 0) {
      input[size - 1] = 0;
    }
    return;
  } else if (codepoint == NULLSPACE_KEY_ENTER) {
    SendInput();
    return;
  } else if (codepoint == NULLSPACE_KEY_PASTE) {
    size_t size = strlen(input);
    platform.PasteClipboard(input + size, NULLSPACE_ARRAY_SIZE(input) - size);
    return;
  }

  if (codepoint < ' ') {
    return;
  } else if (codepoint > '~' && codepoint != 0xDF) {
    switch (codepoint) {
      case 0x160: {
        codepoint = 0x8A;
      } break;
      case 0x161: {
        codepoint = 0x9A;
      } break;
      case 0x178: {
        codepoint = 0x9F;
      } break;
      case 0x17D: {
        codepoint = 0x8E;
      } break;
      case 0x17E: {
        codepoint = 0x9E;
      } break;
      case 0x20AC: {
        codepoint = 0x80;
      } break;
      default: {
      } break;
    }

    if (codepoint > 0xFF) {
      return;
    }
  }

  size_t size = strlen(input);

  if (size >= NULLSPACE_ARRAY_SIZE(input) - 1) {
    return;
  }

  if (size > 0 && codepoint == ':' && input[0] == ':') {
    if (size == 1) {
      char* target = history.GetPrevious(nullptr);

      if (target) {
        sprintf(input, ":%s:", target);
        return;
      }
    } else {
      char* current_target = input + 1;
      char* end = current_target;

      while (*end++) {
        if (*end == ':') {
          char current_name[20];
          size_t namelen = end - current_target;

          sprintf(current_name, "%.*s", (u32)namelen, current_target);

          char* previous = history.GetPrevious(current_name);

          if (previous) {
            sprintf(input, ":%s:", previous);
            return;
          }

#if 0
          for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(private_history); ++i) {
            if (strncmp(current_target, private_history[i], namelen) == 0 && private_history[i][namelen] == 0) {
              size_t index = i - 1;

              if (i == 0) {
                index = private_index - 1;

                if (index > NULLSPACE_ARRAY_SIZE(private_history)) {
                  index = NULLSPACE_ARRAY_SIZE(private_history) - 1;
                }
              }

              if (index == i) {
                // There's only one in history and this one matches
                return;
              }

              assert(index < NULLSPACE_ARRAY_SIZE(private_history));

              sprintf(input, ":%s:", private_history[index]);
              return;
            }
          }
#endif
        }
      }
    }
  }

  input[size] = codepoint;
  input[size + 1] = 0;
}

void ChatController::OnChatPacket(u8* packet, size_t size) {
  ChatType type = (ChatType) * (packet + 1);
  u8 sound = *(packet + 2);
  u16 sender_id = *(u16*)(packet + 3);

  ChatEntry* entry = PushEntry((char*)packet + 5, size - 5, type);

  Player* player = player_manager.GetPlayerById(sender_id);
  if (player) {
    memcpy(entry->sender, player->name, 20);

    if (entry->type == ChatType::Private && player->id != player_manager.player_id) {
      history.InsertRecent(player->name);
    }
  }

  if (entry->type == ChatType::RemotePrivate) {
    if (entry->message[0] == '(') {
      char* sender = entry->message + 1;
      char* current = entry->message;

      while (*current++) {
        if (*current == ')') {
          char name[20];

          sprintf(name, "%.*s", (u32)(current - sender), sender);

          history.InsertRecent(name);
          break;
        }
      }
    }
  }

  entry->sound = sound;
}

ChatEntry* ChatController::PushEntry(const char* mesg, size_t size, ChatType type) {
  ChatEntry* entry = entries + (entry_index++ % NULLSPACE_ARRAY_SIZE(entries));

  memcpy(entry->message, mesg, size);
  entry->sender[0] = 0;
  entry->type = type;
  entry->sound = 0;

  return entry;
}

void ChatController::AddMessage(ChatType type, const char* fmt, ...) {
  ChatEntry* entry = PushEntry("", 0, type);

  va_list args;
  va_start(args, fmt);

  vsprintf(entry->message, fmt, args);

  va_end(args);
}

ChatType ChatController::GetInputType() {
  if (input[0] == 0) return ChatType::Public;

  switch (input[0]) {
    case '\'': {
      return ChatType::Team;
    } break;
    case '/': {
      if (input[1] == '/') {
        return ChatType::Team;
      }
      return ChatType::Private;
    } break;
    case ':': {
      char* ptr = input;
      while (*ptr++) {
        if (*ptr == ':') {
          return ChatType::Private;
        }
      }
    } break;
    case ';': {
      return ChatType::Channel;
    } break;
    case '"': {
      return ChatType::OtherTeam;
    } break;
    default: {
    } break;
  }

  return ChatType::Public;
}

inline void CreateUV(SpriteRenderer& renderer, SpriteRenderable& renderable, const Vector2f& tile_position) {
  Vector2f sheet_dimensions = Graphics::colors.texture_dimensions;

  float left = tile_position.x;
  float right = left + 1.0f;
  float top = tile_position.y;
  float bottom = top + 1.0f;
  float width = sheet_dimensions.x;
  float height = sheet_dimensions.y;

  renderable.uvs[0] = Vector2f(left / width, top / height);
  renderable.uvs[1] = Vector2f(right / width, top / height);
  renderable.uvs[2] = Vector2f(left / width, bottom / height);
  renderable.uvs[3] = Vector2f(right / width, bottom / height);
}

void ChatController::CreateCursor(SpriteRenderer& renderer) {
  SpriteRenderable color = Graphics::GetColor(ColorType::RadarPortal);

  cursor.renderables[0].texture = color.texture;
  cursor.renderables[0].dimensions = Vector2f(1, 12);
  CreateUV(renderer, cursor.renderables[0], Vector2f(0, 25));

  cursor.renderables[1].texture = color.texture;
  cursor.renderables[1].dimensions = Vector2f(1, 12);
  CreateUV(renderer, cursor.renderables[1], Vector2f(0, 0));

  cursor.sprite.duration = 0.5f;
  cursor.sprite.frames = cursor.renderables;
  cursor.sprite.frame_count = 2;

  cursor.animation.t = 0.0f;
  cursor.animation.sprite = &cursor.sprite;
}

void PrivateHistory::InsertRecent(char* name) {
  RecentSenderNode* node = recent;
  RecentSenderNode* alloc_node = nullptr;

  size_t count = 0;

  while (node) {
    ++count;

    if (strcmp(node->name, name) == 0) {
      // Name is already in the list so set this one to the allocation node
      alloc_node = node;

      // Set the count high so it doesn't try to allocate
      count = NULLSPACE_ARRAY_SIZE(nodes);
      break;
    }

    alloc_node = node;
    node = node->next;
  }

  if (count < NULLSPACE_ARRAY_SIZE(nodes)) {
    // Allocate off the nodes until the recent list is fully populated
    node = nodes + count;
  } else {
    // Pop the last node off or the node that was a match for existing name
    RemoveNode(alloc_node);

    node = alloc_node;
  }

  strcpy(node->name, name);
  node->next = recent;
  recent = node;
}

void PrivateHistory::RemoveNode(RecentSenderNode* node) {
  RecentSenderNode* current = recent;

  while (current) {
    if (current->next == node) {
      current->next = node->next;
      break;
    }

    current = current->next;
  }
}

char* PrivateHistory::GetPrevious(char* current) {
  RecentSenderNode* node = recent;

  while (current && node) {
    if (strcmp(node->name, current) == 0) {
      RecentSenderNode* next = node->next;

      // If this is the last node in the list then return the first one
      if (!next) {
        next = recent;
      }

      return next->name;
    }

    node = node->next;
  }

  return recent ? recent->name : nullptr;
}

}  // namespace null
