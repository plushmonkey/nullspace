#ifndef NULLSPACE_NET_CRYPT_H_
#define NULLSPACE_NET_CRYPT_H_

#include <null/Clock.h>
#include <null/Random.h>
#include <null/Types.h>
#include <null/WorkQueue.h>

namespace null {

struct ContinuumEncrypt {
  enum class State {
    // Encryption session hasn't began.
    None,
    // We are waiting for the key to be fully expanded from the network service.
    Expanding,
    // We are waiting for the server to send us an encryption response packet (0x00 0x02).
    Waiting,
    // Table is fully setup and server acknowledged our encryption session.
    Initialized,
  };

  size_t Encrypt(const u8* pkt, u8* dest, size_t size);
  size_t Decrypt(u8* pkt, size_t size);

  void FinalizeExpansion(u32 key);

  State state = State::None;

  u32 expanded_key[20] = {};
  u32 key1 = 0;
  u32 key2 = 0;

  // This is the last tick that we sent 0x00 0x11, we should resend if we don't receive 0x00 0x02.
  u32 key_send_tick = 0;
  u32 resend_count = 0;

  inline bool IsExpanding() const { return state == State::Expanding; }
  inline bool IsInitialized() const { return state == State::Initialized; }
  inline bool ShouldResend() const {
    constexpr s32 kEncryptResendDelay = 100;

    if (state != State::Waiting) return false;

    return TICK_DIFF(GetCurrentTick(), key_send_tick) >= kEncryptResendDelay;
  }
};

struct VieEncrypt {
  size_t Encrypt(const u8* pkt, u8* dest, size_t size);
  size_t Decrypt(u8* pkt, size_t size);

  bool Initialize(u32 server_key);
  bool IsValidKey(u32 server_key);

  static u32 GenerateKey();

  u32 session_key = 0;
  u32 client_key = 0;
  VieRNG rng;
  char keystream[520];
};

}  // namespace null

#endif
