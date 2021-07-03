#ifndef NULLSPACE_RENDER_COLORS_H_
#define NULLSPACE_RENDER_COLORS_H_

#include "Animation.h"
#include "Sprite.h"

namespace null {

constexpr float kColorTextureWidth = 128.0f;
constexpr float kColorTextureHeight = 40.0f;

enum class ColorType {
  Blank,

  Border1,
  Border2,
  Border3,

  Background = 16,
  Tile = 18,
  Safe = 19,
  RadarPrize = 23,
  RadarSelf = 27,
  RadarTeam = 29,
  RadarEnemyFlag = 31,
  RadarEnemyTarget = 33,
  RadarEnemy = 34,
  RadarTeamDecoy = 36,
};

struct Colors {
  u32 texture_id = -1;
  float animation_time = 0.0f;

  void Update(float dt);
  SpriteRenderable GetRenderable(ColorType type);
  SpriteRenderable GetRenderable(ColorType type, const Vector2f& dimensions);
};

}  // namespace null

#endif
