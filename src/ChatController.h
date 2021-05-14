#ifndef NULLSPACE_CHAT_H_
#define NULLSPACE_CHAT_H_

#include "InputState.h"
#include "Types.h"

namespace null {

struct Camera;
struct Connection;
struct PacketDispatcher;
struct PlayerManager;
struct SpriteRenderer;
struct StatBox;

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
  Channel,
  Fuchsia = 79
};

struct ChatEntry {
  ChatType type;
  u8 sound;
  char sender[20];
  char message[520];
};

struct ChatController {
  Connection& connection;
  PlayerManager& player_manager;
  StatBox& statbox;

  size_t entry_index = 0;
  ChatEntry entries[64] = {};
  bool display_full = false;
  // Continuum has input of 256 but only 250 get sent?
  // Limiting to 250 to make the chat work exactly as would send instead of emulating Continuum's bad behavior
  char input[250] = {0};

  ChatController(PacketDispatcher& dispatcher, Connection& connection, PlayerManager& player_manager, StatBox& statbox);

  void Render(Camera& camera, SpriteRenderer& renderer);
  ChatEntry* PushEntry(const char* mesg, size_t size, ChatType type);
  void AddMessage(ChatType type, const char* fmt, ...);

  void OnChatPacket(u8* packet, size_t size);
  void OnCharacterPress(int codepoint, int mods);

  void SendInput();
  bool HandleInputCommands();
  ChatType GetInputType();
};

}  // namespace null

#endif
