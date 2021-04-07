#include "Render.h"

#include <GLFW/glfw3.h>

#include <cstdio>

#include "Map.h"
#include "Memory.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace null {

const Vector2f kGridVertices[] = {
    Vector2f(0, 0), Vector2f(0, 1024), Vector2f(1024, 0), Vector2f(1024, 0), Vector2f(0, 1024), Vector2f(1024, 1024),
};

const char* kVertexShaderCode = R"(
#version 330

layout (location = 0) in vec2 position;

uniform mat4 mvp;

out vec2 varying_position;

void main() {
  gl_Position = mvp * vec4(position, 0.0, 1.0);
  varying_position = position;
}
)";

const char* kFragmentShaderCode = R"(
#version 330

in vec2 varying_position;

uniform sampler2DArray tilemap;
// TODO: This could be packed instead of uint for values that can only be 0-255
uniform usampler2D tiledata;

out vec4 color;

void main() {
  ivec2 fetchp = ivec2(varying_position);
  uint tile_id = texelFetch(tiledata, fetchp, 0).r;

  if (tile_id == 0u || tile_id > 190u) {
    discard;
  }

  float row = float((tile_id - 1u) % 19u);
  float col = float((tile_id - 1u) / 19u);

  // Calculate uv by getting fraction of traversed tile
  vec2 uv = (varying_position - floor(varying_position));

  // Calculate the sample position in the tilemap for this tile
  //vec2 sample_position = (vec2(row, col) * vec2(1.0 / 19.0, 1.0 / 10.0)) + uv;

  color = texture(tilemap, vec3(uv, tile_id - 1u));
}
)";

RenderState::RenderState() : camera(0, 0) {}

bool RenderState::Initialize(int width, int height) {
  camera.surface_width = (float)width;
  camera.surface_height = (float)height;

  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize window system.\n");
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, false);
  glfwWindowHint(GLFW_SAMPLES, 8);

  window = glfwCreateWindow(width, height, "nullspace", NULL, NULL);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    fprintf(stderr, "Failed to initialize opengl context");
    return false;
  }

  glViewport(0, 0, width, height);

  if (!tile_shader.Initialize(kVertexShaderCode, kFragmentShaderCode)) {
    fprintf(stderr, "Failed to load shaders.\n");
    return false;
  }

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &grid_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kGridVertices), kGridVertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vector2f), 0);
  glEnableVertexAttribArray(0);

  glfwSwapInterval(0);

  tilemap_uniform = glGetUniformLocation(tile_shader.program, "tilemap");
  tiledata_uniform = glGetUniformLocation(tile_shader.program, "tiledata");
  mvp_uniform = glGetUniformLocation(tile_shader.program, "mvp");

  return true;
}

bool RenderState::CreateMapBuffer(MemoryArena& arena, const char* filename) {
  FILE* file = fopen(filename, "rb");

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &tilemap_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tilemap_texture);

  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  int width, height, comp;

  // TODO: Load default tilemap if there's none in the map
  u32* tilemap = (u32*)stbi_load_from_file(file, &width, &height, &comp, STBI_rgb_alpha);
  assert(tilemap);

  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, 190, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

  // Create a 3d texture to prevent uv bleed
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

  stbi_image_free(tilemap);

  fclose(file);

  glActiveTexture(GL_TEXTURE1);
  glGenTextures(1, &tiledata_texture);
  glBindTexture(GL_TEXTURE_2D, tiledata_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

  Map map;

  if (!map.Load(arena, filename)) {
    fprintf(stderr, "Could not load map for rendering.\n");
    return false;
  }

  int* tiledata = memory_arena_push_type_count(&arena, int, 1024 * 1024);
  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      tiledata[y * 1024 + x] = map.GetTileId(x, y);
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, tiledata);

  return true;
}

bool RenderState::Render(float dt) {
  if (tiledata_texture == -1) return true;
  // Temporary until key expansion request is threaded
  if (dt > 1) return true;

  glfwPollEvents();

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(tile_shader.program);

  glActiveTexture(GL_TEXTURE0);
  glActiveTexture(GL_TEXTURE1);

  glUniform1i(tilemap_uniform, 0);
  glUniform1i(tiledata_uniform, 1);

  static const Vector2f kBasePosition(512.0f, 512.f);
  static float timer;

  timer += dt * 0.3f;

  camera.position = kBasePosition + Vector2f(std::cosf(timer) * 45.0f, std::sinf(timer) * 45.0f);
  camera.zoom = 0.0625f;

  mat4 proj = camera.GetProjection();
  mat4 view = camera.GetView();
  mat4 mvp = proj * view;

  glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, (const GLfloat*)mvp.data);

  glDrawArrays(GL_TRIANGLES, 0, 6);

  glUseProgram(0);

  glfwSwapBuffers(window);

  if (glfwWindowShouldClose(window)) {
    glfwTerminate();
    return false;
  }

  return true;
}

bool CreateShader(GLenum type, const char* source, GLuint* shaderOut) {
  GLuint shader = glCreateShader(type);

  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success;
  GLchar info_log[512];

  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

  if (!success) {
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    fprintf(stderr, "Shader error: %s\n", info_log);
    return false;
  }

  *shaderOut = shader;
  return true;
}

bool CreateProgram(GLuint vertexShader, GLuint fragmentShader, GLuint* programOut) {
  GLuint program = glCreateProgram();

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  GLint success;
  GLchar info_log[512];

  glGetProgramiv(program, GL_LINK_STATUS, &success);

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  if (!success) {
    glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
    fprintf(stderr, "Program link error: %s\n", info_log);
    return false;
  }

  *programOut = program;
  return true;
}

bool ShaderProgram::Initialize(const char* vertex_code, const char* fragment_code) {
  GLuint vertex_shader, fragment_shader;

  if (!CreateShader(GL_VERTEX_SHADER, vertex_code, &vertex_shader)) {
    return false;
  }

  if (!CreateShader(GL_FRAGMENT_SHADER, fragment_code, &fragment_shader)) {
    glDeleteShader(vertex_shader);
    return false;
  }

  return CreateProgram(vertex_shader, fragment_shader, &program);
}

}  // namespace null
