#ifndef NULLSPACE_SOCCER_H_
#define NULLSPACE_SOCCER_H_

#include "Types.h"

namespace null {

struct Connection;
struct SpectateView;
struct Vector2f;

// TODO: Implement
struct Soccer {
  Connection& connection;
  SpectateView& specview;

  Soccer(Connection& connection, SpectateView& specview);

  bool IsTeamGoal(const Vector2f& position);
};

}  // namespace null

#endif
