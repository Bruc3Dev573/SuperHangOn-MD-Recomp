/*
 * Frontend settings kept in settings.ini in the game directory.
 */
#ifndef SETTINGS_H
#define SETTINGS_H
#include "video.h"

enum { VSYNC_AUTO, VSYNC_ON, VSYNC_OFF, VSYNC_COUNT };

typedef struct { int w, h; const char *label; } WindowPreset;
extern const WindowPreset window_presets[];
extern const int window_preset_count;
extern const int render_heights[];
extern const int render_height_count;

typedef struct {
  VideoSettings video;
  int fullscreen;
  int window_preset;        /* index into window_presets */
  int render_index;         /* index into render_heights (0 = window) */
  int vsync;                /* VSYNC_* */
  int show_fps;
  int volume;               /* 0..100 */
  int frame_rate;           /* 60 or 120: logic ticks and video frames per second (changed while running) */
} AppSettings;

void settings_default(AppSettings *s);
void settings_load(AppSettings *s, const char *path);
void settings_save(const AppSettings *s, const char *path);

#endif
