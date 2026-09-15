/*
 * UI layer drawn over the picture: a 640x448 ARGB canvas (twice the game
 * resolution) with pixel fonts, boxes, the frame rate counter and the
 * settings menu.
 */
#ifndef UI_LAYER_H
#define UI_LAYER_H
#include <stdint.h>

#define UI_W 640
#define UI_H 448

extern uint32_t ui_canvas[UI_H][UI_W];

void ui_clear(void);
void ui_box(int x, int y, int w, int h, uint32_t argb);
/* 5x7 font, `scale` canvas pixels per font pixel, 6 font pixels per character */
void ui_text(int x, int y, int scale, uint32_t argb, const char *text);
int ui_text_width(int scale, const char *text);

#endif
