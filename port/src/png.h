#ifndef PNG_H
#define PNG_H
#include <stdint.h>
int png_write_rgb32(const char *path, const uint32_t *pixels, int w, int h, int stride);
#endif
