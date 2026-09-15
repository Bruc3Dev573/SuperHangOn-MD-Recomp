#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

#define MD_MAX_W 320
#define MD_MAX_H 224

extern uint32_t render_frame_rgb[MD_MAX_H][MD_MAX_W];   /* 0x00RRGGBB */
extern int render_width;                                /* 320 (H40) or 256 (H32) */
extern int render_gpgx_colours;                         /* quantise like the GPGX tracer */

uint32_t render_colour(unsigned index);                /* CRAM entry as 0x00RRGGBB */
void render_frame(void);

/* layers of a wide race picture, 320 + 2 ext columns (column i shows x =
 * i - ext), 0xAARRGGBB: plane B and backdrop (back), plane A and window by
 * priority (lo: under the sprites, hi: over them; alpha 0 where empty). The
 * window's HUD groups are moved to the edges. render_wide_effects is nonzero
 * when the frame has what the sides would not show: high priority plane A
 * pixels outside the window or plane B pixels in the 4:3 columns, or a window
 * that is more than a HUD at the top (the dithered fade at the start of a
 * race). H40 frames only. */
#define RENDER_WIDE_MAX 640
extern uint32_t render_wide_back[MD_MAX_H][RENDER_WIDE_MAX];
extern uint32_t render_wide_plane_lo[MD_MAX_H][RENDER_WIDE_MAX];
extern uint32_t render_wide_plane_hi[MD_MAX_H][RENDER_WIDE_MAX];
extern int render_wide_effects;
void render_frame_wide(int ext);
void render_line(int y, uint16_t vscroll_a, uint16_t vscroll_b, uint32_t *out, int width);

#endif
