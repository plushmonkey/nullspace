#include "Soccer.h"

#include "SpectateView.h"
#include "net/Connection.h"

namespace null {

Soccer::Soccer(Connection& connection, SpectateView& specview) : connection(connection), specview(specview) {}

bool OnMode3(const Vector2f& position, u32 frequency) {
  u32 corner = frequency % 4;

  switch (corner) {
    case 0: {
      return position.x < 512 && position.y < 512;
    } break;
    case 1: {
      return position.x >= 512 && position.y < 512;
    } break;
    case 2: {
      return position.x < 512 && position.y >= 512;
    } break;
    case 3: {
      return position.x >= 512 && position.y >= 512;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool OnMode5(const Vector2f& position, u32 frequency) {
  u32 direction = frequency % 4;

  switch (direction) {
    case 0: {
      if (position.y < 512) {
        return position.x < position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 1: {
      if (position.x < 512) {
        return position.x + position.y >= 1024;
      }

      return position.x < position.y;
    } break;
    case 2: {
      if (position.x < 512) {
        return position.x >= position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 3: {
      if (position.y <= 512) {
        return position.x + position.y >= 1024;
      }

      return position.x >= position.y;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool Soccer::IsTeamGoal(const Vector2f& position) {
  u32 frequency = specview.GetFrequency();

  switch (connection.settings.SoccerMode) {
    case 0: {
      return false;
    } break;
    case 1: {
      if (frequency & 1) {
        return position.x >= 512;
      }

      return position.x < 512;
    } break;
    case 2: {
      if (frequency & 1) {
        return position.y >= 512;
      }

      return position.y < 512;
    } break;
    case 3: {
      return OnMode3(position, frequency);
    } break;
    case 4: {
      return !OnMode3(position, frequency);
    } break;
    case 5: {
      return OnMode5(position, frequency);
    } break;
    case 6: {
      return !OnMode5(position, frequency);
    }
    default: {
    } break;
  }

  return true;
}

}  // namespace null
