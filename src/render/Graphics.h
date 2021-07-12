#ifndef NULLSPACE_RENDER_GRAPHICS_H_
#define NULLSPACE_RENDER_GRAPHICS_H_

#include "../Math.h"
#include "Animation.h"
#include "Colors.h"
#include "Sprite.h"

namespace null {

struct Colors;
struct SpriteRenderer;

struct Graphics {
  static Colors colors;

  static SpriteRenderable* character_set[256];

  static SpriteRenderable* text_sprites;
  static SpriteRenderable* textf_sprites;
  static SpriteRenderable* energyfont_sprites;

  static SpriteRenderable* ship_sprites;
  static SpriteRenderable* turret_sprites;
  static SpriteRenderable* spectate_sprites;

  static SpriteRenderable* warp_sprites;

  static SpriteRenderable* explode0_sprites;
  static SpriteRenderable* explode1_sprites;
  static SpriteRenderable* explode2_sprites;
  static SpriteRenderable* emp_burst_sprites;

  static SpriteRenderable* bomb_sprites;
  static SpriteRenderable* bomb_trail_sprites;
  static SpriteRenderable* mine_sprites;
  static SpriteRenderable* shrapnel_sprites;
  static SpriteRenderable* bombflash_sprites;
  static SpriteRenderable* emp_spark_sprites;

  static SpriteRenderable* bullet_sprites;
  static SpriteRenderable* bullet_trail_sprites;

  static SpriteRenderable* repel_sprites;
  static SpriteRenderable* brick_sprites;
  static SpriteRenderable* portal_sprites;
  static SpriteRenderable* super_sprites;
  static SpriteRenderable* shield_sprites;
  static SpriteRenderable* flag_indicator_sprites;

  static SpriteRenderable* icon_sprites;
  static SpriteRenderable empty_icon_sprites[2];
  static SpriteRenderable* icon_count_sprites;

  static SpriteRenderable* exhaust_sprites;
  static SpriteRenderable* rocket_sprites;

  static SpriteRenderable* prize_sprites;
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
  static AnimatedSprite anim_bombflash;
  static AnimatedSprite anim_emp_spark;

  static AnimatedSprite anim_mines[4];
  static AnimatedSprite anim_emp_mines[4];

  static AnimatedSprite anim_shrapnel[3];
  static AnimatedSprite anim_bounce_shrapnel[3];

  static AnimatedSprite anim_bullets[4];
  static AnimatedSprite anim_bullet_explode;
  static AnimatedSprite anim_bullets_bounce[4];
  static AnimatedSprite anim_bullet_trails[4];
  static AnimatedSprite anim_burst_inactive;
  static AnimatedSprite anim_burst_active;

  static AnimatedSprite anim_repel;
  static AnimatedSprite anim_enemy_brick;
  static AnimatedSprite anim_team_brick;
  static AnimatedSprite anim_portal;
  static AnimatedSprite anim_super;
  static AnimatedSprite anim_shield;
  static AnimatedSprite anim_flag_indicator;

  static AnimatedSprite anim_ship_explode;
  static AnimatedSprite anim_ship_warp;
  static AnimatedSprite anim_ship_exhaust;
  static AnimatedSprite anim_ship_rocket;

  static AnimatedSprite anim_prize;
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
  static void CreateMineAnimations(SpriteRenderable* renderables, int count);
  static void CreateBombTrailAnimations(SpriteRenderable* renderables, int count);
  static void CreateBombExplodeAnimations(SpriteRenderable* renderables, int count);
  static void CreateEmpExplodeAnimations(SpriteRenderable* renderables, int count);
  static void CreateEmpSparkAnimations(SpriteRenderable* renderables, int count);
  static void CreateBulletAnimations(SpriteRenderable* renderables, int count);
  static void CreateBulletTrailAnimations(SpriteRenderable* renderables, int count);
  static void CreateRepelAnimations(SpriteRenderable* renderables, int count);
  static void CreatePrizeAnimations(SpriteRenderable* renderables, int count);
  static void CreateFlagAnimations(SpriteRenderable* renderables, int count);
  static void CreateExhaustAnimations(SpriteRenderable* renderables, int count);
  static void CreateRocketAnimations(SpriteRenderable* renderables, int count);
  static void CreatePortalAnimations(SpriteRenderable* renderables, int count);
  static void CreateBrickAnimations(SpriteRenderable* renderables, int count);

  static inline SpriteRenderable GetColor(ColorType type) { return colors.GetRenderable(type); }
  static inline SpriteRenderable GetColor(ColorType type, const Vector2f& dimensions) {
    return colors.GetRenderable(type, dimensions);
  }

  static inline SpriteRenderable GetColor(size_t index) { return colors.GetRenderable((ColorType)index); }
  static inline SpriteRenderable GetColor(size_t index, const Vector2f& dimensions) {
    return colors.GetRenderable((ColorType)index, dimensions);
  }

 private:
  Graphics() {}

  static bool InitializeFont(SpriteRenderer& renderer);
  static bool InitializeWeapons(SpriteRenderer& renderer);
  static bool InitializeTiles(SpriteRenderer& renderer);
};

}  // namespace null

#endif
