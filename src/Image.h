#ifndef NULLSPACE_IMAGE_H_
#define NULLSPACE_IMAGE_H_

namespace null {

// Image loading function that typically passes to stb_image.
// It also implements RLE BMP loading if that fails.
unsigned char* ImageLoad(const char* filename, int* width, int* height);
void ImageFree(void* data);

}

#endif