#include "ChatController.h"

#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "PlayerManager.h"
#include "StatBox.h"
#include "Tick.h"
#include "net/Connection.h"
#include "net/PacketDispatcher.h"
#include "render/Camera.h"
#include "render/SpriteRenderer.h"

namespace null {

// TODO: Configurable namelen
constexpr u32 namelen = 10;
constexpr float kFontWidth = 8.0f;
constexpr float kFontHeight = 12.0f;

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

  if (type == ChatType::Private) {
    Player* selected = statbox.GetSelectedPlayer();

    if (selected) {
      target = selected->id;
    }

    if (mesg[0] == '/') {
      ++mesg;
    } else if (mesg[0] == ':') {
      while (*mesg++) {
        if (*mesg == ':') {
          ++mesg;
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

struct ChatSpan {
  const char* begin;
  const char* end;
};

void WrapChat(const char* mesg, s32 linesize, ChatSpan* lines, size_t* linecount) {
  // Trim front
  while (*mesg && *mesg == ' ') ++mesg;

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
      end = last_end + size;
      lines[count - 1].begin = mesg + last_end;
      lines[count - 1].end = mesg + end;
      *linecount = count;
      break;
    }

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

    if (end <= last_end) {
      end = last_end + linesize;
    }

    lines[count - 1].begin = mesg + last_end;
    lines[count - 1].end = mesg + end;

    last_end = end;
    *linecount = count;

    // Trim again for next line
    while (last_end < size && mesg[last_end] == ' ') {
      ++last_end;
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

    WrapChat(input, max_characters, lines, &linecount);

    y -= kFontHeight * linecount;

    char output[512];
    for (size_t i = 0; i < linecount; ++i) {
      ChatSpan* span = lines + i;
      u32 length = (u32)(span->end - span->begin);

      sprintf(output, "%.*s", length, span->begin);

      renderer.DrawText(camera, output, color, Vector2f(0, y + i * kFontHeight));
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
      case ChatType::Fuchsia:
      case ChatType::Arena: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          TextColor color = entry->type == ChatType::Fuchsia ? TextColor::Fuschia : TextColor::Green;

          renderer.DrawText(camera, output, color, Vector2f(0, y + j * kFontHeight));
        }
      } break;
      case ChatType::PublicMacro:
      case ChatType::Public: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%*s> %.*s", namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Blue, Vector2f(0, y + j * kFontHeight));
        }
      } break;
      case ChatType::Team: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%*s> %.*s", namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Yellow, Vector2f(0, y + j * kFontHeight));
        }
      } break;
      case ChatType::OtherTeam: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%*s> ", namelen, entry->sender);
          float skip = strlen(output) * kFontWidth;
          renderer.DrawText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight));

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::Blue, Vector2f(skip, y + j * kFontHeight));
        }
      } break;
      case ChatType::Private: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%*s> %.*s", namelen, entry->sender, length, span->begin);

          renderer.DrawText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight));
        }
      } break;
      case ChatType::RedWarning:
      case ChatType::RedError: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::DarkRed, Vector2f(0, y + j * kFontHeight));
        }
      } break;
      case ChatType::Channel: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.DrawText(camera, output, TextColor::Red, Vector2f(0, y + j * kFontHeight));
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
    PasteClipboard(input + size, NULLSPACE_ARRAY_SIZE(input) - size);
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
      char* ptr = input + 1;
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

}  // namespace null
