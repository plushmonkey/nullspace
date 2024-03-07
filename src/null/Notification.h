#ifndef NULLSPACE_NOTIFICATION_H_
#define NULLSPACE_NOTIFICATION_H_

#include <null/Types.h>
#include <null/render/SpriteRenderer.h>

namespace null {

struct Camera;
struct SpriteRenderer;

struct GameNotification {
  char message[256];
  TextColor color;
  u32 end_tick;
};

struct NotificationSystem {
  NotificationSystem();

  GameNotification notifications[7];

  void Render(Camera& camera, SpriteRenderer& renderer);

  // Refreshes the oldest notification and returns it so the message can be set.
  GameNotification* PushNotification(TextColor color);
  GameNotification* PushFormatted(TextColor color, const char* fmt, ...);
};

}  // namespace null

#endif
