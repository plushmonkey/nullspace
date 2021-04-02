#ifndef NULLSPACE_PLAYER_H_
#define NULLSPACE_PLAYER_H_

#include "Math.h"
#include "Types.h"

namespace null {

using PlayerId = u16;

struct WeaponData {
  u16 type : 5;
  u16 level : 2;
  u16 shrapbouncing : 1;
  u16 shraplevel : 2;
  u16 shrap : 5;
  u16 alternate : 1;
};

struct Player {
  char name[32];
  char squad[20];

  s32 flag_points;
  s32 kill_points;

  PlayerId player_id;
  u16 frequency;

  Vector2f position;
  Vector2f velocity;

  u16 wins;
  u16 losses;

  u16 bounty;
  u16 energy;

  u8 ship;
  u8 direction;
  u8 togglables;
  u8 ping;

  WeaponData weapon;
};

}  // namespace null

#endif
