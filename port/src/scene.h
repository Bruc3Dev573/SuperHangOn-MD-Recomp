/*
 * Race scene rebuilt for wide screen formats: the picture of the game's 320
 * columns extended by `ext` columns on each side.
 *
 * Road: the game draws it on plane B from a road picture in perspective (one
 * row per depth step), choosing per line the row (vertical scroll), the
 * curve offset (horizontal scroll) and one of two copies with different
 * palettes (stripes). scene_build() takes the per-step tables the game used
 * for the displayed frame (screen height of every step in 1/16 line, curve
 * offset in 1/64 pixel), fits straight lines to the colour boundaries of the
 * road picture in VRAM and emits one trapezoid per colour run per step, drawn
 * far to near so that hills hide the road behind them. The road can then be
 * drawn to any width. It checks the tables against the line scroll values
 * the VDP displayed.
 *
 * Sprites: the objects are put in the game's sprite table order (depth
 * bucket, slot) and matched to the sprite table the VDP displayed; entries
 * that belong to no object (messages such as the checkpoint text) are kept
 * as single sprites in their place. Each object is drawn as an image composed
 * from its mapping and the VRAM tiles, at its position; roadside objects
 * beside the 4:3 screen, which the game does not put in the table, are drawn
 * too. The game hides sprites behind hills by filling the VDP's sprite pixel
 * limit on the lines below the crest (object type $44); here an object is cut
 * where the rebuilt road shows a nearer depth step.
 */
#ifndef SCENE_H
#define SCENE_H
#include <stdint.h>

/* trapezoid with horizontal top (y0) and bottom (y1) edges, in pixels of the
 * 320x224 picture (x may be negative or beyond 320): top edge x00..x01,
 * bottom edge x10..x11 */
typedef struct {
  float y0, y1, x00, x01, x10, x11;
  uint32_t rgb;
} SceneQuad;

/* sprite image: 0xAARRGGBB (alpha 0 or 255), top row first; id changes
 * whenever the pixels do */
typedef struct {
  int id, w, h;
  const uint32_t *pixels;
} SceneImage;

/* rows v0..v1 (0..1) of an image placed over x0..x1, y0..y1; an object cut
 * by a hill gives several */
typedef struct {
  float x0, y0, x1, y1, v0, v1;
  int image;
} SceneSprite;

#define SCENE_MAX_QUADS 4096
#define SCENE_MAX_SPRITES 160
#define SCENE_MAX_IMAGES 128

/* the picture is render_wide_back, the quads, render_wide_plane_lo, the
 * sprites (far to near), render_wide_plane_hi */
typedef struct {
  int ext;
  int nquads, nsprites, nimages;
  SceneQuad quad[SCENE_MAX_QUADS];
  SceneSprite sprite[SCENE_MAX_SPRITES];
  SceneImage image[SCENE_MAX_IMAGES];
} Scene;

/* copy of the game state the next displayed frame is built from; call at the
 * end of every frame (after the VBlank interrupt) */
void scene_frame_end(void);

/* builds the wide picture of the frame being displayed, after
 * render_frame_wide(ext); returns 0 when the frame cannot be shown wide (no
 * race road, sprites not accounted for, screen effects that only cover the
 * 4:3 columns) */
int scene_build(Scene *scene, int ext);

/* software rasteriser for tests: the picture at `scale` times, 0x00RRGGBB,
 * (320 + 2 ext) scale x 224 scale */
void scene_rasterize(const Scene *scene, uint32_t *out, int scale);

#endif
