#include "TileRenderer.h"

#include <cassert>
#include <cstdio>

#include "../Map.h"
#include "../Math.h"
#include "../Memory.h"
#include "Camera.h"
#include "Image.h"

namespace null {

const Vector2f kGridVertices[] = {
    Vector2f(0, 0), Vector2f(0, 1024), Vector2f(1024, 0), Vector2f(1024, 0), Vector2f(0, 1024), Vector2f(1024, 1024),
};

const char* kGridVertexShaderCode = R"(#version 300 es

in vec2 position;

uniform mat4 mvp;

out vec2 varying_position;

void main() {
  gl_Position = mvp * vec4(position, 3.0, 1.0);
  varying_position = position;
}
)";

const char* kGridFragmentShaderCode = R"(#version 300 es
precision mediump float;
precision mediump int;
precision mediump sampler2DArray;
precision mediump usampler2D;

in vec2 varying_position;

// This tilemap needs to be 3d for clamping to edge of tiles. There's some bleeding if it's 2d
uniform sampler2DArray tilemap;
// TODO: This could be packed instead of uint for values that can only be 0-255
uniform usampler2D tiledata;

out vec4 color;

void main() {
  ivec2 fetchp = ivec2(varying_position);
  uint tile_id = texelFetch(tiledata, fetchp, 0).r;

  if (tile_id == 0u || tile_id == 170u || tile_id == 172u || tile_id > 190u || (tile_id >= 162u && tile_id <= 169u)) {
    discard;
  }

  // Calculate uv by getting fraction of traversed tile
  vec2 uv = (varying_position - floor(varying_position));

  color = texture(tilemap, vec3(uv, tile_id - 1u));
  if (tile_id > 172u && color.r == 0.0 && color.g == 0.0 && color.b == 0.0) {
    discard;
  }
}
)";

bool TileRenderer::Initialize() {
  if (!shader.Initialize(kGridVertexShaderCode, kGridFragmentShaderCode)) {
    fprintf(stderr, "Failed to load tile shader.\n");
    return false;
  }

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kGridVertices), kGridVertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vector2f), 0);
  glEnableVertexAttribArray(0);

  glGenTextures(1, &tiledata_texture);
  glBindTexture(GL_TEXTURE_2D, tiledata_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

  tilemap_uniform = glGetUniformLocation(shader.program, "tilemap");
  tiledata_uniform = glGetUniformLocation(shader.program, "tiledata");
  mvp_uniform = glGetUniformLocation(shader.program, "mvp");

  shader.Use();

  glUniform1i(tilemap_uniform, 0);
  glUniform1i(tiledata_uniform, 1);

  return true;
}

void TileRenderer::Render(Camera& camera) {
  if (tiledata_texture == -1 || tilemap_texture == -1) return;

  shader.Use();
  glBindVertexArray(vao);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tilemap_texture);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, tiledata_texture);

  mat4 proj = camera.GetProjection();
  mat4 view = camera.GetView();
  mat4 mvp = proj * view;

  glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, (const GLfloat*)mvp.data);

  glDrawArrays(GL_TRIANGLES, 0, 6);
}

bool TileRenderer::CreateMapBuffer(MemoryArena& temp_arena, const char* filename, const Vector2f& surface_dim) {
  // Create and setup tilemap color texture
  glBindVertexArray(vao);

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &tilemap_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tilemap_texture);

  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  int width, height;

  u32* tilemap = (u32*)ImageLoad(filename, &width, &height, false);

  if (!tilemap) {
    tilemap = (u32*)ImageLoad("graphics/tiles.bm2", &width, &height);
  }

  if (!tilemap) {
    fprintf(stderr, "Failed to load tilemap.\n");
    return false;
  }

  // Create a 3d texture to prevent uv bleed
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, 190, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

  for (int tile_y = 0; tile_y < 10; ++tile_y) {
    for (int tile_x = 0; tile_x < 19; ++tile_x) {
      int tile_id = tile_y * 19 + tile_x;
      u32 data[16 * 16];

      int base_y = tile_y * 16 * 16 * 19;
      int base_x = tile_x * 16;

      for (int copy_y = 0; copy_y < 16; ++copy_y) {
        for (int copy_x = 0; copy_x < 16; ++copy_x) {
          u32 tilemap_index = base_y + base_x + copy_y * 16 * 19 + copy_x;

          data[copy_y * 16 + copy_x] = tilemap[tilemap_index];
        }
      }

      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, tile_id, 16, 16, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
  }

  glGenTextures(1, &door_texture);
  glBindTexture(GL_TEXTURE_2D, door_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  u32 door_width = 16 * 8;
  u32 door_height = 16;

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();
  u32* door_data = (u32*)temp_arena.Allocate(door_width * door_height * 4);

  for (size_t y = 0; y < door_height; ++y) {
    for (size_t x = 0; x < door_width; ++x) {
      size_t tilemap_x = x + 9 * 16;
      size_t tilemap_y = y + 8 * 16;
      size_t tilemap_index = tilemap_y * 16 * 19 + tilemap_x;
      size_t door_index = y * door_width + x;

      door_data[door_index] = tilemap[tilemap_index];
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, door_width, door_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, door_data);

  temp_arena.Revert(snapshot);
  ImageFree(tilemap);

  // Setup tile id data
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, tiledata_texture);

  Map map;

  // Load the map so the tile id data can be sent to gpu
  if (!map.Load(temp_arena, filename)) {
    fprintf(stderr, "Could not load map for rendering.\n");
    return false;
  }

  int* tiledata = memory_arena_push_type_count(&temp_arena, int, 1024 * 1024);
  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      tiledata[y * 1024 + x] = map.GetTileId(x, y);
    }
  }

  // Store entire map tile id data on gpu
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, tiledata);

  temp_arena.Revert(snapshot);
  return true;
}

bool TileRenderer::CreateRadar(MemoryArena& temp_arena, const char* filename, const Vector2f& surface_dim,
                               u16 mapzoom) {
  Map map;

  if (!map.Load(temp_arena, filename)) {
    fprintf(stderr, "Could not load map for radar rendering.\n");
    return false;
  }

  u32 surface_width = (u32)surface_dim.x;
  u32 surface_height = (u32)surface_dim.y;

  if (surface_width <= 64 * 2) {
    fprintf(stderr, "Surface width too small to generate radar.\n");
    return false;
  }

  u32 radar_dim = (u32)((surface_width / 2.0f) - 64.0f);

  if (mapzoom < 1) {
    mapzoom = 1;
  }

  u32 full_dim = (surface_width * 8) / mapzoom;

  if (full_radar_texture != -1) {
    glDeleteTextures(1, &full_radar_texture);
    full_radar_texture = -1;
  }

  if (radar_texture != -1) {
    glDeleteTextures(1, &radar_texture);
    radar_texture = -1;
  }

  RenderRadar(map, temp_arena, full_dim, full_radar_renderable, &full_radar_texture, GL_NEAREST);
  RenderRadar(map, temp_arena, radar_dim, radar_renderable, &radar_texture, GL_NEAREST);

  return true;
}

void TileRenderer::RenderRadar(Map& map, MemoryArena& temp_arena, u32 dimensions, SpriteRenderable& renderable,
                               GLuint* texture, GLint filter) {
  glGenTextures(1, texture);
  glBindTexture(GL_TEXTURE_2D, *texture);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dimensions, dimensions, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

  if (filter != GL_NEAREST && filter != GL_LINEAR) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();
  u32* data = (u32*)temp_arena.Allocate(dimensions * dimensions * sizeof(u32));

  if (dimensions < 1024) {
    for (u16 y = 0; y < dimensions; ++y) {
      for (u16 x = 0; x < dimensions; ++x) {
        size_t index = y * dimensions + x;
        data[index] = 0xFF0A190A;
      }
    }
    for (u16 y = 0; y < 1024; ++y) {
      u16 dest_y = (u16)((y / 1024.0f) * dimensions);

      for (u16 x = 0; x < 1024; ++x) {
        u16 dest_x = (u16)((x / 1024.0f) * dimensions);

        TileId id = map.GetTileId(x, y);

        size_t index = dest_y * dimensions + dest_x;

        if (id == 0 || id > 241) {
          // Don't write because multiple writes to this area should prioritize walls. If a write happens here then
          // walls could be overwritten with empty space.
        } else {
          data[index] = GetRadarTileColor(id);
        }
      }
    }
  } else {
    u32 y_tile_index = 0;

    for (size_t gen_y = 0; gen_y < dimensions; ++gen_y) {
      u16 y = y_tile_index / dimensions;

      u32 x_tile_index = 0;
      for (size_t gen_x = 0; gen_x < dimensions; ++gen_x) {
        u16 x = x_tile_index / dimensions;
        TileId id = map.GetTileId(x, y);

        size_t index = gen_y * dimensions + gen_x;

        data[index] = GetRadarTileColor(id);

        x_tile_index += 1024;
      }

      y_tile_index += 1024;
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dimensions, dimensions, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

  temp_arena.Revert(snapshot);

  if (filter != GL_NEAREST && filter != GL_LINEAR) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  renderable.texture = *texture;

  renderable.dimensions = Vector2f((float)dimensions, (float)dimensions);
  renderable.uvs[0] = Vector2f(0, 0);
  renderable.uvs[1] = Vector2f(1, 0);
  renderable.uvs[2] = Vector2f(0, 1);
  renderable.uvs[3] = Vector2f(1, 1);
}

u32 TileRenderer::GetRadarTileColor(u8 id) {
  // TODO: other types
  if (id == 0 || id > 241) {
    return 0xFF0A190A;
  } else if (id == 171) {
    return 0xFF185218;
  } else if (id == 172) {
    // TODO: team color
    return 0xFF0839FF;
  } else if (id >= 162 && id <= 169) {
    return 0xFFADADAD;
  }

  return 0xFF5a5a5a;
}

void TileRenderer::Cleanup() {
  shader.Cleanup();
  glDeleteTextures(1, &door_texture);
  glDeleteTextures(1, &tilemap_texture);
  glDeleteTextures(1, &tiledata_texture);
  glDeleteTextures(1, &radar_texture);
  glDeleteTextures(1, &full_radar_texture);

  if (vao != -1) {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    vao = -1;
  }

  door_texture = -1;
  tilemap_texture = -1;
  tiledata_texture = -1;
  radar_texture = -1;
  full_radar_texture = -1;
}

}  // namespace null
