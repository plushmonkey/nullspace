#include "Graphics.h"

#include "SpriteRenderer.h"

namespace null {

constexpr float kBombAnimDuration = 1.0f;
constexpr float kMineAnimDuration = 1.0f;

SpriteRenderable* Graphics::ship_sprites = nullptr;
SpriteRenderable* Graphics::spectate_sprites = nullptr;

SpriteRenderable* Graphics::warp_sprites = nullptr;
SpriteRenderable* Graphics::explode1_sprites = nullptr;

SpriteRenderable* Graphics::bomb_sprites = nullptr;
SpriteRenderable* Graphics::bomb_trail_sprites = nullptr;
SpriteRenderable* Graphics::mine_sprites = nullptr;

SpriteRenderable* Graphics::bullet_sprites = nullptr;
SpriteRenderable* Graphics::bullet_trail_sprites = nullptr;

SpriteRenderable* Graphics::repel_sprites = nullptr;

AnimatedSprite Graphics::anim_bombs[4];
AnimatedSprite Graphics::anim_bomb_trails[4];
AnimatedSprite Graphics::anim_emp_bombs[4];
AnimatedSprite Graphics::anim_thor;

AnimatedSprite Graphics::anim_mines[4];
AnimatedSprite Graphics::anim_emp_mines[4];

AnimatedSprite Graphics::anim_bullets[4];
AnimatedSprite Graphics::anim_bullets_bounce[4];
AnimatedSprite Graphics::anim_bullet_trails[4];

AnimatedSprite Graphics::anim_repel;

AnimatedSprite Graphics::anim_ship_explode;
AnimatedSprite Graphics::anim_ship_warp;

bool Graphics::Initialize(SpriteRenderer& renderer) {
  int count;

  ship_sprites = renderer.LoadSheet("graphics/ships.bm2", Vector2f(36, 36), &count);
  if (!ship_sprites) return false;

  spectate_sprites = renderer.LoadSheet("graphics/spectate.bm2", Vector2f(8, 8), &count);
  if (!spectate_sprites) return false;

  warp_sprites = renderer.LoadSheet("graphics/warp.bm2", Vector2f(48, 48), &count);
  if (!warp_sprites) return false;

  anim_ship_warp.frames = warp_sprites;
  anim_ship_warp.frame_count = count;
  anim_ship_warp.duration = 0.5f;

  explode1_sprites = renderer.LoadSheet("graphics/explode1.bm2", Vector2f(48, 48), &count);
  if (!explode1_sprites) return false;

  anim_ship_explode.frames = explode1_sprites;
  anim_ship_explode.frame_count = count;
  anim_ship_explode.duration = 1.0f;

  bomb_sprites = renderer.LoadSheet("graphics/bombs.bm2", Vector2f(16, 16), &count);
  if (!bomb_sprites) return false;

  for (size_t i = 0; i < 4; ++i) {
    anim_bombs[i].frames = bomb_sprites + i * 10;
    anim_bombs[i].frame_count = 10;
    anim_bombs[i].duration = kBombAnimDuration;
  }

  for (size_t i = 0; i < 4; ++i) {
    anim_emp_bombs[i].frames = bomb_sprites + i * 10 + 40;
    anim_emp_bombs[i].frame_count = 10;
    anim_emp_bombs[i].duration = kBombAnimDuration;
  }

  anim_thor.frames = bomb_sprites + 120;
  anim_thor.frame_count = 10;
  anim_thor.duration = kBombAnimDuration;

  bomb_trail_sprites = renderer.LoadSheet("graphics/trail.bm2", Vector2f(16, 16), &count);
  if (!bomb_trail_sprites) return false;

  for (size_t i = 0; i < 4; ++i) {
    anim_bomb_trails[i].frames = bomb_trail_sprites + i * 10;
    anim_bomb_trails[i].frame_count = 10;
    anim_bomb_trails[i].duration = 0.3f;
  }

  mine_sprites = renderer.LoadSheet("graphics/mines.bm2", Vector2f(16, 16), &count);
  if (!mine_sprites) return false;

  for (size_t i = 0; i < 4; ++i) {
    anim_mines[i].frames = mine_sprites + i * 10;
    anim_mines[i].frame_count = 10;
    anim_mines[i].duration = kMineAnimDuration;
  }

  for (size_t i = 0; i < 4; ++i) {
    anim_emp_mines[i].frames = mine_sprites + i * 10 + 40;
    anim_emp_mines[i].frame_count = 10;
    anim_emp_mines[i].duration = kMineAnimDuration;
  }

  bullet_sprites = renderer.LoadSheet("graphics/bullets.bm2", Vector2f(5, 5), &count);
  if (!bullet_sprites) return false;

  for (size_t i = 0; i < 4; ++i) {
    anim_bullets[i].frames = bullet_sprites + i * 4;
    anim_bullets[i].frame_count = 4;
    anim_bullets[i].duration = 0.1f;
  }

  for (size_t i = 0; i < 4; ++i) {
    anim_bullets_bounce[i].frames = bullet_sprites + i * 4 + 20;
    anim_bullets_bounce[i].frame_count = 4;
    anim_bullets_bounce[i].duration = 0.1f;
  }

  bullet_trail_sprites = renderer.LoadSheet("graphics/gradient.bm2", Vector2f(1, 1), &count);
  if (!bullet_trail_sprites) return false;

  for (size_t i = 0; i < 3; ++i) {
    anim_bullet_trails[2 - i].frames = bullet_trail_sprites + i * 14;
    anim_bullet_trails[2 - i].frame_count = 7;
    anim_bullet_trails[2 - i].duration = 0.1f;
  }
  anim_bullet_trails[3] = anim_bullet_trails[2];

  repel_sprites = renderer.LoadSheet("graphics/repel.bm2", Vector2f(96, 96), &count);
  if (!repel_sprites) return false;

  anim_repel.duration = 0.5f;
  anim_repel.frames = repel_sprites;
  anim_repel.frame_count = count;

  return true;
}

}  // namespace null
