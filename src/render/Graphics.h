#ifndef NULLSPACE_RENDER_GRAPHICS_H_
#define NULLSPACE_RENDER_GRAPHICS_H_

#include "Animation.h"
#include "Sprite.h"

namespace null {

struct SpriteRenderer;

struct Graphics {
  static SpriteRenderable* ship_sprites;
  static SpriteRenderable* spectate_sprites;

  static SpriteRenderable* warp_sprites;
  static SpriteRenderable* explode1_sprites;

  static SpriteRenderable* bomb_sprites;
  static SpriteRenderable* bomb_trail_sprites;
  static SpriteRenderable* mine_sprites;

  static SpriteRenderable* bullet_sprites;
  static SpriteRenderable* bullet_trail_sprites;

  static SpriteRenderable* repel_sprites;

  static AnimatedSprite anim_bombs[4];
  static AnimatedSprite anim_bomb_trails[4];
  static AnimatedSprite anim_emp_bombs[4];
  static AnimatedSprite anim_thor;

  static AnimatedSprite anim_mines[4];
  static AnimatedSprite anim_emp_mines[4];

  static AnimatedSprite anim_bullets[4];
  static AnimatedSprite anim_bullets_bounce[4];
  static AnimatedSprite anim_bullet_trails[4];

  static AnimatedSprite anim_repel;

  static AnimatedSprite anim_ship_explode;
  static AnimatedSprite anim_ship_warp;

  static bool Initialize(SpriteRenderer& renderer);

 private:
  Graphics() {}
};

}  // namespace null

#endif
