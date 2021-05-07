#ifndef NULLSPACE_RENDER_GRAPHICS_H_
#define NULLSPACE_RENDER_GRAPHICS_H_

#include "../Math.h"
#include "Animation.h"
#include "Sprite.h"

namespace null {

struct SpriteRenderer;

constexpr size_t kSeparatorColorIndex = 1;
constexpr size_t kBackgroundColorIndex = 16;

struct Graphics {
  static SpriteRenderable* character_set[256];

  static SpriteRenderable* text_sprites;
  static SpriteRenderable* textf_sprites;
  static SpriteRenderable* energyfont_sprites;

  static SpriteRenderable* ship_sprites;
  static SpriteRenderable* spectate_sprites;

  static SpriteRenderable* warp_sprites;

  static SpriteRenderable* explode0_sprites;
  static SpriteRenderable* explode1_sprites;
  static SpriteRenderable* explode2_sprites;
  static SpriteRenderable* emp_burst_sprites;

  static SpriteRenderable* bomb_sprites;
  static SpriteRenderable* bomb_trail_sprites;
  static SpriteRenderable* mine_sprites;

  static SpriteRenderable* bullet_sprites;
  static SpriteRenderable* bullet_trail_sprites;

  static SpriteRenderable* repel_sprites;

  static SpriteRenderable* color_sprites;
  static SpriteRenderable* icon_sprites;

  static SpriteRenderable* flag_sprites;
  static SpriteRenderable* goal_sprites;
  static SpriteRenderable* asteroid_small1_sprites;
  static SpriteRenderable* asteroid_small2_sprites;
  static SpriteRenderable* asteroid_large_sprites;
  static SpriteRenderable* space_station_sprites;
  static SpriteRenderable* wormhole_sprites;

  static AnimatedSprite anim_bombs[4];
  static AnimatedSprite anim_emp_bombs[4];
  static AnimatedSprite anim_bombs_bounceable[4];
  static AnimatedSprite anim_bomb_explode;
  static AnimatedSprite anim_emp_explode;
  static AnimatedSprite anim_thor;
  static AnimatedSprite anim_bomb_trails[4];

  static AnimatedSprite anim_mines[4];
  static AnimatedSprite anim_emp_mines[4];

  static AnimatedSprite anim_bullets[4];
  static AnimatedSprite anim_bullet_explode;
  static AnimatedSprite anim_bullets_bounce[4];
  static AnimatedSprite anim_bullet_trails[4];

  static AnimatedSprite anim_repel;

  static AnimatedSprite anim_ship_explode;
  static AnimatedSprite anim_ship_warp;

  static AnimatedSprite anim_flag;
  static AnimatedSprite anim_flag_team;
  static AnimatedSprite anim_goal;
  static AnimatedSprite anim_goal_team;
  static AnimatedSprite anim_asteroid_small1;
  static AnimatedSprite anim_asteroid_small2;
  static AnimatedSprite anim_asteroid_large;
  static AnimatedSprite anim_space_station;
  static AnimatedSprite anim_wormhole;

  static bool Initialize(SpriteRenderer& renderer);

  static void DrawBorder(SpriteRenderer& renderer, Camera& camera, const Vector2f& center,
                         const Vector2f& half_extents);

  static void CreateBombAnimations(SpriteRenderable* renderables, int count);
  static void CreateBombTrailAnimations(SpriteRenderable* renderables, int count);
  static void CreateBombExplodeAnimations(SpriteRenderable* renderables, int count);
  static void CreateEmpExplodeAnimations(SpriteRenderable* renderables, int count);
  static void CreateBulletAnimations(SpriteRenderable* renderables, int count);
  static void CreateBulletTrailAnimations(SpriteRenderable* renderables, int count);
  static void CreateRepelAnimations(SpriteRenderable* renderables, int count);
  static void CreateFlagAnimations(SpriteRenderable* renderables, int count);

 private:
  Graphics() {}

  static bool InitializeFont(SpriteRenderer& renderer);
  static bool InitializeWeapons(SpriteRenderer& renderer);
  static bool InitializeTiles(SpriteRenderer& renderer);
};

}  // namespace null

#endif
