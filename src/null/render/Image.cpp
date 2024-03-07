#include "Image.h"

#include <null/Memory.h>
#include <null/Platform.h>
#include <null/Types.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
//
#include <assert.h>
#include <memory.h>
#include <stdio.h>

namespace null {

#pragma pack(push, 1)
struct BitmapFileHeader {
  unsigned char signature[2];
  u32 size;
  u16 reserved1;
  u16 reserved2;
  u32 offset;
};

struct DIBHeader {
  u32 header_size;
  s32 width;
  s32 height;
  u16 planes;
  u16 bpp;
  u32 compression;
  u32 image_size;
  u32 x_ppm;
  u32 y_ppm;
  u32 color_table_count;
};
#pragma pack(pop)

inline u32 GetColor(u32* color_table, u32 index) {
  if (color_table == nullptr) {
    return kDefaultColorTable[index];
  }

  return color_table[index];
}

// TODO: Use arenas intead of new allocations
unsigned char* LoadBitmap(const u8* data, size_t file_size, int* width, int* height) {
  BitmapFileHeader* file_header = (BitmapFileHeader*)data;
  DIBHeader* dib_header = (DIBHeader*)(data + sizeof(BitmapFileHeader));

  // Only support 8 bit RLE for now
  if (dib_header->compression != 1) {
    return nullptr;
  }

  assert(dib_header->bpp == 8);
  if (dib_header->bpp != 8) return nullptr;

  *width = abs(dib_header->width);
  *height = abs(dib_header->height);

  // Seek to color table
  const u8* ptr = data + sizeof(BitmapFileHeader) + dib_header->header_size;

  u32* color_table = nullptr;

  if (dib_header->color_table_count > 0) {
    color_table = (u32*)ptr;
  }

  // Seek to image data
  ptr = data + file_header->offset;

  u32* result = nullptr;

  if (dib_header->bpp == 8) {
    size_t image_size = dib_header->width * dib_header->height;
    const u8* image_data = ptr;

    // Expand out the data to rgba
    result = (u32*)malloc(image_size * sizeof(u32));
    if (!result) return nullptr;

    memset(result, 0, image_size * sizeof(u32));
    size_t i = 0;
    int x = 0;
    int y = dib_header->height - 1;

    while (i < image_size) {
      u8 count = image_data[i++];
      u8 color_index = image_data[i++];

      if (count == 0) {
        if (color_index == 0) {
          // End of line
          --y;
          x = 0;
        } else if (color_index == 1) {
          // End of bitmap
          break;
        } else if (color_index == 2) {
          // Delta
          int next_x = image_data[i++];
          int next_y = image_data[i++];

          x += next_x;
          y -= next_y;

          assert(x >= 0 && x < dib_header->width);
          assert(y >= 0 && y < dib_header->height);
        } else {
          // Run of absolute values
          for (int j = 0; j < color_index; ++j) {
            u8 absolute_index = image_data[i++];

            u32 color = GetColor(color_table, absolute_index) | 0xFF000000;
            color = ((color & 0xFF) << 16) | ((color & 0x00FF0000) >> 16) | (color & 0xFF000000) | (color & 0x0000FF00);
            result[y * dib_header->width + x] = color;
            ++x;
          }

          if (i & 1) {
            ++i;
          }
        }
      } else {
        u32 color = GetColor(color_table, color_index) | 0xFF000000;
        color = ((color & 0xFF) << 16) | ((color & 0x00FF0000) >> 16) | (color & 0xFF000000) | (color & 0x0000FF00);

        for (int j = 0; j < count; ++j) {
          size_t index = (size_t)y * (size_t)dib_header->width + (size_t)x;
          result[index] = color;
          ++x;
        }
      }
    }
  }

  return (u8*)result;
}

unsigned char* ImageLoad(const char* filename, int* width, int* height, bool asset) {
  size_t size;
  u8* data = nullptr;

  if (asset) {
    data = platform.LoadAsset(filename, &size);
  } else {
    FILE* f = fopen(filename, "rb");

    if (f) {
      fseek(f, 0, SEEK_END);
      size = (size_t)ftell(f);
      fseek(f, 0, SEEK_SET);

      data = (u8*)malloc(size);

      fread(data, 1, size, f);

      fclose(f);
    }
  }

  if (!data) {
    return nullptr;
  }

  int comp;
  unsigned char* result = stbi_load_from_memory(data, (int)size, width, height, &comp, STBI_rgb_alpha);

  if (result == nullptr) {
    // Try to load RLE bitmap
    result = LoadBitmap(data, size, width, height);
  }

  free(data);

  return result;
}

unsigned char* ImageLoadFromMemory(const u8* data, size_t size, int* width, int* height) {
  int comp;
  unsigned char* result = stbi_load_from_memory(data, (int)size, width, height, &comp, STBI_rgb_alpha);

  if (result == nullptr) {
    result = LoadBitmap(data, size, width, height);
  }

  return result;
}

void ImageFree(void* data) {
  stbi_image_free(data);
}

}  // namespace null
