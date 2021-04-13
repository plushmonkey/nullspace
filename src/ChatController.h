#ifndef NULLSPACE_CHAT_H_
#define NULLSPACE_CHAT_H_

#include "InputState.h"
#include "Types.h"

namespace null {

struct Camera;
struct Connection;
struct SpriteRenderer;
struct PacketDispatcher;

enum class ChatType {
  Arena,
  PublicMacro,
  Public,
  Team,
  OtherTeam,
  Private,
  RedWarning,
  RemotePrivate,
  RedError,
  Channel
};

struct ChatEntry {
  ChatType type;
  u8 sound;
  char sender[20];
  char message[520];
};

struct ChatController {
  Connection& connection;

  size_t entry_index = 0;
  ChatEntry entries[64] = {};
  bool display_full = false;
  // Continuum has input of 256 but only 250 get sent?
  // Limiting to 250 to make the chat work exactly as would send instead of emulating Continuum's bad behavior
  char input[250] = {0};

  ChatController(PacketDispatcher& dispatcher, Connection& connection);

  void Render(Camera& camera, SpriteRenderer& renderer);
  ChatEntry* PushEntry(const char* mesg, size_t size, ChatType type);

  void OnChatPacket(u8* packet, size_t size);
  void OnCharacterPress(char c, bool control);

  void SendInput();
  ChatType GetInputType();
};

}  // namespace null

#endif
