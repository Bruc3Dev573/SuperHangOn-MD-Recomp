/*
 * On-screen text for the frontend (frame rate counter): a 3x5 pixel font
 * drawn into the rendered frame, on a dark box.
 */
#include <ctype.h>
#include "overlay.h"

/* 5 rows of 3 pixels, one octal digit per row, top row first */
static const uint16_t glyphs[128] = {
  ['0'] = 075557, ['1'] = 026227, ['2'] = 071747, ['3'] = 071317, ['4'] = 055711,
  ['5'] = 074717, ['6'] = 074757, ['7'] = 071111, ['8'] = 075757, ['9'] = 075717,
  ['A'] = 025755, ['B'] = 065656, ['C'] = 074447, ['D'] = 065556, ['E'] = 074647,
  ['F'] = 074644, ['G'] = 074557, ['H'] = 055755, ['I'] = 072227, ['J'] = 011157,
  ['K'] = 055655, ['L'] = 044447, ['M'] = 057755, ['N'] = 065555, ['O'] = 075557,
  ['P'] = 075744, ['Q'] = 075571, ['R'] = 075655, ['S'] = 074717, ['T'] = 072222,
  ['U'] = 055557, ['V'] = 055552, ['W'] = 055775, ['X'] = 055255, ['Y'] = 055222,
  ['Z'] = 071247, ['.'] = 000002, [':'] = 002020, ['-'] = 000700, ['/'] = 011244,
};

void overlay_text(uint32_t (*frame)[OVERLAY_MAX_W], int width, int height, int x, int y, const char *text)
{
  int n = 0;
  for (const char *p = text; *p; p++)
    n++;
  for (int yy = y - 1; yy < y + 6; yy++)              /* dark box, 1 pixel margin */
    for (int xx = x - 1; xx < x + n * 4; xx++)
      if (yy >= 0 && yy < height && xx >= 0 && xx < width)
        frame[yy][xx] = 0xc0000000;   /* translucent on the UI layer */
  for (int i = 0; i < n; i++) {
    unsigned char ch = (unsigned char)toupper((unsigned char)text[i]);
    uint16_t g = ch < 128 ? glyphs[ch] : 0;
    for (int row = 0; row < 5; row++)
      for (int col = 0; col < 3; col++)
        if (g & (1u << ((4 - row) * 3 + (2 - col)))) {
          int px = x + i * 4 + col, py = y + row;
          if (px < width && py < height)
            frame[py][px] = 0xffffff40;
        }
  }
}
