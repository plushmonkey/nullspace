#include "Notification.h"

#include <null/Clock.h>
#include <null/render/Camera.h>
//
#include <stdarg.h>
#include <stdio.h>

namespace null {

constexpr u32 kNotificationDuration = 500;

NotificationSystem::NotificationSystem() {
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(notifications); ++i) {
    notifications[i].end_tick = 0;
  }
}

void NotificationSystem::Render(Camera& camera, SpriteRenderer& renderer) {
  Vector2f position(camera.surface_dim.x * 0.2f, camera.surface_dim.y * 0.6f);

  u32 tick = GetCurrentTick();

  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(notifications); ++i) {
    GameNotification* notification = notifications + i;

    if (TICK_GT(notification->end_tick, tick)) {
      renderer.DrawText(camera, notification->message, notification->color, position, Layer::Chat);
    }

    position.y += 12.0f;
  }

  renderer.Render(camera);
}

GameNotification* NotificationSystem::PushNotification(TextColor color) {
  GameNotification* best = notifications;
  u32 tick = GetCurrentTick();

  // Loop through each possible notification area and find the best match.
  // The first one that isn't visible is used or the one with the oldest tick otherwise.
  for (size_t i = 0; i < NULLSPACE_ARRAY_SIZE(notifications); ++i) {
    GameNotification* notification = notifications + i;

    if (TICK_GT(tick, notification->end_tick)) {
      best = notification;
      break;
    }

    if (notification->end_tick < best->end_tick) {
      best = notification;
    }
  }

  best->end_tick = GetCurrentTick() + kNotificationDuration;
  best->color = color;

  return best;
}

GameNotification* NotificationSystem::PushFormatted(TextColor color, const char* fmt, ...) {
  GameNotification* notification = PushNotification(color);

  va_list args;
  va_start(args, fmt);
  vsprintf(notification->message, fmt, args);
  va_end(args);

  return notification;
}

}  // namespace null
