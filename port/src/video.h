/*
 * Presentation of the rendered game frame with OpenGL: output resolution,
 * scaling, aspect ratio and CRT emulation shaders (shadow mask, slot mask,
 * aperture grille, scanlines, glow, curvature), plus a UI layer drawn on top.
 */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>
#include <SDL.h>
#include "render.h"
#include "scene.h"

enum { SCALE_INTEGER, SCALE_FIT, SCALE_STRETCH, SCALE_COUNT };
enum { FORMAT_4_3, FORMAT_16_9, FORMAT_21_9, FORMAT_COUNT };
enum { CRT_OFF, CRT_SCANLINES, CRT_APERTURE_GRILLE, CRT_SLOT_MASK, CRT_SHADOW_MASK, CRT_COUNT };

typedef struct {
  int render_height;        /* processing resolution: 0 = the window itself, else 224..2160 */
  int scale_mode;           /* SCALE_* */
  int aspect_43;            /* 1: TV pixels (320x224 shown as 4:3), 0: square pixels */
  int crt;                  /* CRT_* */
  float scanlines;          /* 0..1 depth of the gaps between scanlines */
  float mask_strength;      /* 0..1 */
  int mask_tvl;             /* 0: one phosphor column per output pixel (x1 at 1080 lines); else 450..900 TVL */
  float glow;               /* 0..1 */
  float curvature;          /* 0..1 */
  float vignette;           /* 0..1 */
  float sharpness;          /* 0 soft .. 1 sharp (horizontal) */
  float brightness;         /* 0.5..2, compensates the mask */
  int screen_format;        /* FORMAT_*: selected output aspect */
} VideoSettings;

/* a wide race picture: the scene and the layers of render_frame_wide()
 * (RENDER_WIDE_MAX pixels per row); scene NULL: show the last one again */
typedef struct {
  const Scene *scene;
  const uint32_t *back, *plane_lo, *plane_hi;
} VideoWide;

void video_default_settings(VideoSettings *s);

/* columns added on each side of the game's 320 for the screen format */
int video_wide_ext(const VideoSettings *s);

/* creates the GL context on `window` (created with SDL_WINDOW_OPENGL);
 * returns 0 on success, -1 if OpenGL 3.3 core is not available */
int video_init(SDL_Window *window, int vsync);
void video_shutdown(void);

/* draws the game frame (0x00RRGGBB pixels, `stride` pixels per row) with the
 * settings, then the UI layer (RGBA, may be NULL), and presents. A wide race
 * picture is shown when given; frames without one fill the selected format. */
void video_present(const uint32_t *frame, int w, int h, int stride, const VideoSettings *s,
                   const VideoWide *wide, const uint32_t *ui, int ui_w, int ui_h);

/* capture the next presented image (window size, 0x00RRGGBB, top row first);
 * video_captured returns it once available, then clears it */
void video_capture_next(void);
const uint32_t *video_captured(int *w, int *h);

#endif
