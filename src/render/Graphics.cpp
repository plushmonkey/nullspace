#include "Graphics.h"

#include "SpriteRenderer.h"

namespace null {

constexpr float kBombAnimDuration = 1.0f;
constexpr float kMineAnimDuration = 1.0f;

SpriteRenderable* Graphics::text_sprites = nullptr;
SpriteRenderable* Graphics::textf_sprites = nullptr;

SpriteRenderable* Graphics::ship_sprites = nullptr;
SpriteRenderable* Graphics::spectate_sprites = nullptr;

SpriteRenderable* Graphics::warp_sprites = nullptr;

SpriteRenderable* Graphics::explode0_sprites = nullptr;
SpriteRenderable* Graphics::explode1_sprites = nullptr;
SpriteRenderable* Graphics::explode2_sprites = nullptr;
SpriteRenderable* Graphics::emp_burst_sprites = nullptr;

SpriteRenderable* Graphics::bomb_sprites = nullptr;
SpriteRenderable* Graphics::bomb_trail_sprites = nullptr;
SpriteRenderable* Graphics::mine_sprites = nullptr;

SpriteRenderable* Graphics::bullet_sprites = nullptr;
SpriteRenderable* Graphics::bullet_trail_sprites = nullptr;

SpriteRenderable* Graphics::repel_sprites = nullptr;

SpriteRenderable* Graphics::color_sprites = nullptr;

AnimatedSprite Graphics::anim_bombs[4];
AnimatedSprite Graphics::anim_emp_bombs[4];
AnimatedSprite Graphics::anim_bombs_bounceable[4];
AnimatedSprite Graphics::anim_bomb_explode;
AnimatedSprite Graphics::anim_emp_explode;
AnimatedSprite Graphics::anim_thor;
AnimatedSprite Graphics::anim_bomb_trails[4];

AnimatedSprite Graphics::anim_mines[4];
AnimatedSprite Graphics::anim_emp_mines[4];

AnimatedSprite Graphics::anim_bullets[4];
AnimatedSprite Graphics::anim_bullet_explode;
AnimatedSprite Graphics::anim_bullets_bounce[4];
AnimatedSprite Graphics::anim_bullet_trails[4];

AnimatedSprite Graphics::anim_repel;

AnimatedSprite Graphics::anim_ship_explode;
AnimatedSprite Graphics::anim_ship_warp;

SpriteRenderable* Graphics::character_set[256] = {};

bool Graphics::Initialize(SpriteRenderer& renderer) {
  int count;

  if (!InitializeFont(renderer)) {
    return false;
  }

  if (!InitializeWeapons(renderer)) {
    return false;
  }

  color_sprites = renderer.LoadSheet("graphics/colors.bm2", Vector2f(128, 1), &count);
  if (!color_sprites) return false;

  ship_sprites = renderer.LoadSheet("graphics/ships.bm2", Vector2f(36, 36), &count);
  if (!ship_sprites) return false;

  spectate_sprites = renderer.LoadSheet("graphics/spectate.bm2", Vector2f(8, 8), &count);
  if (!spectate_sprites) return false;

  warp_sprites = renderer.LoadSheet("graphics/warp.bm2", Vector2f(48, 48), &count);
  if (!warp_sprites) return false;

  anim_ship_warp.frames = warp_sprites;
  anim_ship_warp.frame_count = count;
  anim_ship_warp.duration = 0.5f;

  explode0_sprites = renderer.LoadSheet("graphics/explode0.bm2", Vector2f(16, 16), &count);
  if (!explode0_sprites) return false;

  anim_bullet_explode.frames = explode0_sprites;
  anim_bullet_explode.frame_count = count;
  anim_bullet_explode.duration = 0.2f;

  explode1_sprites = renderer.LoadSheet("graphics/explode1.bm2", Vector2f(48, 48), &count);
  if (!explode1_sprites) return false;

  anim_ship_explode.frames = explode1_sprites;
  anim_ship_explode.frame_count = count;
  anim_ship_explode.duration = 1.0f;

  explode2_sprites = renderer.LoadSheet("graphics/explode2.bm2", Vector2f(80, 80), &count);
  if (!explode2_sprites) return false;

  anim_bomb_explode.frames = explode2_sprites;
  anim_bomb_explode.frame_count = count;
  anim_bomb_explode.duration = 1.25f;

  emp_burst_sprites = renderer.LoadSheet("graphics/empburst.bm2", Vector2f(80, 80), &count);
  if (!emp_burst_sprites) return false;

  anim_emp_explode.frames = emp_burst_sprites;
  anim_emp_explode.frame_count = count;
  anim_emp_explode.duration = 0.5f;

  return true;
}

bool Graphics::InitializeFont(SpriteRenderer& renderer) {
  int count;

  text_sprites = renderer.LoadSheet("graphics/tallfont.bm2", Vector2f(8, 12), &count);
  if (!text_sprites) return false;

  textf_sprites = renderer.LoadSheet("graphics/tallfontf.bm2", Vector2f(8, 12), &count);
  if (!textf_sprites) return false;

  for (size_t i = ' '; i < '~'; ++i) {
    character_set[i] = Graphics::text_sprites + i - ' ';
  }

  for (size_t i = 0xC0; i < 0xD7; ++i) {
    character_set[i] = Graphics::textf_sprites + i - 0xC0 + 5;
  }

  size_t index = 28;
  for (size_t i = 0xD8; i < 0xF7; ++i) {
    character_set[i] = Graphics::textf_sprites + index++;
  }

  index = 59;
  // 0xFF maps to _ on asss due to a bug with asss (should be y with diaeresis)
  for (size_t i = 0xF8; i <= 0xFF; ++i) {
    character_set[i] = Graphics::textf_sprites + index++;
  }

  character_set[0xDF] = Graphics::text_sprites + '~' - ' ' + 1;

  // windows-1252 encoding
  character_set[0x80] = Graphics::textf_sprites + 35;
  character_set[0x8A] = Graphics::textf_sprites;
  character_set[0x8E] = Graphics::textf_sprites + 1;
  character_set[0x9A] = Graphics::textf_sprites + 2;
  character_set[0x9E] = Graphics::textf_sprites + 3;
  character_set[0x9F] = Graphics::textf_sprites + 4;

  return true;
}

bool Graphics::InitializeWeapons(SpriteRenderer& renderer) {
  int count;

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

  for (size_t i = 0; i < 4; ++i) {
    anim_bombs_bounceable[i].frames = bomb_sprites + i * 10 + 80;
    anim_bombs_bounceable[i].frame_count = 10;
    anim_bombs_bounceable[i].duration = kBombAnimDuration;
  }

  anim_thor.frames = bomb_sprites + 120;
  anim_thor.frame_count = 10;
  anim_thor.duration = kBombAnimDuration;

  bomb_trail_sprites = renderer.LoadSheet("graphics/trail.bm2", Vector2f(16, 16), &count);
  if (!bomb_trail_sprites) return false;

  for (size_t i = 0; i < 4; ++i) {
    anim_bomb_trails[i].frames = bomb_trail_sprites + i * 10;
    anim_bomb_trails[i].frame_count = 10;
    anim_bomb_trails[i].duration = 0.35f;
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
    anim_bullets[i].duration = 0.15f;
  }

  for (size_t i = 0; i < 4; ++i) {
    anim_bullets_bounce[i].frames = bullet_sprites + i * 4 + 20;
    anim_bullets_bounce[i].frame_count = 4;
    anim_bullets_bounce[i].duration = 0.15f;
  }

  bullet_trail_sprites = renderer.LoadSheet("graphics/gradient.bm2", Vector2f(1, 1), &count);
  if (!bullet_trail_sprites) return false;

  for (size_t i = 0; i < 3; ++i) {
    anim_bullet_trails[i].frames = bullet_trail_sprites + i * 14 + 3 * 14;
    anim_bullet_trails[i].frame_count = 14;
    anim_bullet_trails[i].duration = 0.15f;
  }
  anim_bullet_trails[3] = anim_bullet_trails[2];

  repel_sprites = renderer.LoadSheet("graphics/repel.bm2", Vector2f(96, 96), &count);
  if (!repel_sprites) return false;

  anim_repel.duration = 0.5f;
  anim_repel.frames = repel_sprites;
  anim_repel.frame_count = count;

  return true;
}

void Graphics::DrawBorder(SpriteRenderer& renderer, Camera& camera, const Vector2f& center,
                          const Vector2f& half_extents) {
  SpriteRenderable renderable;

  renderable.texture = Graphics::color_sprites[1].texture;
  renderable.dimensions = Vector2f(1, half_extents.y * 2 + 2);

  for (size_t i = 0; i < 4; ++i) {
    renderable.uvs[i] = Graphics::color_sprites[1].uvs[i];
  }

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 1, -half_extents.y - 1));
  renderer.Draw(camera, renderable, center + Vector2f(half_extents.x, -half_extents.y - 1));

  renderable.dimensions = Vector2f(half_extents.x * 2 + 2, 1);

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 1, -half_extents.y - 1));
  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 1, half_extents.y));

  renderable.texture = Graphics::color_sprites[2].texture;
  for (size_t i = 0; i < 4; ++i) {
    renderable.uvs[i] = Graphics::color_sprites[2].uvs[i];
  }
  renderable.dimensions = Vector2f(1, half_extents.y * 2 + 4);

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 2, -half_extents.y - 2));
  renderer.Draw(camera, renderable, center + Vector2f(half_extents.x + 1, -half_extents.y - 2));

  renderable.dimensions = Vector2f(half_extents.x * 2 + 4, 1);

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 2, -half_extents.y - 2));
  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 2, half_extents.y + 1));

  renderable.texture = Graphics::color_sprites[3].texture;
  for (size_t i = 0; i < 4; ++i) {
    renderable.uvs[i] = Graphics::color_sprites[3].uvs[i];
  }
  renderable.dimensions = Vector2f(1, half_extents.y * 2 + 2);

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 3, -half_extents.y - 1));
  renderer.Draw(camera, renderable, center + Vector2f(half_extents.x + 2, -half_extents.y - 1));

  renderable.dimensions = Vector2f(half_extents.x * 2 + 2, 1);

  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 1, -half_extents.y - 3));
  renderer.Draw(camera, renderable, center + Vector2f(-half_extents.x - 1, half_extents.y + 2));
}

}  // namespace null
