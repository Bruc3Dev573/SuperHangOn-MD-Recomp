#ifndef OVERLAY_H
#define OVERLAY_H
#include <stdint.h>

#define OVERLAY_MAX_W 320

/* draws text (digits, A-Z, . : - /) at (x, y), 4 pixels per character */
void overlay_text(uint32_t (*frame)[OVERLAY_MAX_W], int width, int height, int x, int y, const char *text);

#endif
