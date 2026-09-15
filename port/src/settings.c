#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"

const WindowPreset window_presets[] = {
  {320, 224, "320X224 (NATIVE)"},
  {640, 448, "640X448 (2X)"},
  {960, 672, "960X672 (3X)"},
  {1280, 896, "1280X896 (4X)"},
  {1280, 720, "1280X720"},
  {1920, 1080, "1920X1080"},
  {2560, 1440, "2560X1440"},
  {3840, 2160, "3840X2160 (4K)"},
};
const int window_preset_count = sizeof window_presets / sizeof window_presets[0];

const int render_heights[] = {0, 224, 448, 720, 1080, 1440, 2160};
const int render_height_count = sizeof render_heights / sizeof render_heights[0];

void settings_default(AppSettings *s)
{
  memset(s, 0, sizeof *s);
  video_default_settings(&s->video);
  s->fullscreen = 0;
  s->window_preset = 2;
  s->render_index = 0;
  s->vsync = VSYNC_AUTO;
  s->fps_limit = GAME_FPS_60;
  s->show_fps = 1;
  s->volume = 100;
  s->music_track = 0;
}

/* key = value lines; unknown keys are ignored */
typedef struct { const char *key; int is_float; void *(*field)(AppSettings *); } Entry;

#define INT_FIELD(name, expr) static void *f_##name(AppSettings *s) { return &s->expr; }
#define FLT_FIELD(name, expr) static void *f_##name(AppSettings *s) { return &s->expr; }
INT_FIELD(fullscreen, fullscreen)
INT_FIELD(window, window_preset)
INT_FIELD(render, render_index)
INT_FIELD(vsync, vsync)
INT_FIELD(fps_limit, fps_limit)
INT_FIELD(fps, show_fps)
INT_FIELD(volume, volume)
INT_FIELD(music_track, music_track)
INT_FIELD(scale, video.scale_mode)
INT_FIELD(aspect, video.aspect_43)
INT_FIELD(crt, video.crt)
INT_FIELD(tvl, video.mask_tvl)
INT_FIELD(format, video.screen_format)
FLT_FIELD(scanlines, video.scanlines)
FLT_FIELD(mask, video.mask_strength)
FLT_FIELD(glow, video.glow)
FLT_FIELD(curvature, video.curvature)
FLT_FIELD(vignette, video.vignette)
FLT_FIELD(sharpness, video.sharpness)
FLT_FIELD(brightness, video.brightness)

static const Entry entries[] = {
  {"fullscreen", 0, f_fullscreen}, {"window_preset", 0, f_window}, {"render_resolution", 0, f_render},
  {"vsync", 0, f_vsync}, {"fps_limit", 0, f_fps_limit}, {"show_fps", 0, f_fps}, {"volume", 0, f_volume},
  {"music_track", 0, f_music_track},
  {"screen_format", 0, f_format}, {"scaling", 0, f_scale}, {"aspect_4_3", 0, f_aspect}, {"crt_filter", 0, f_crt}, {"crt_mask_tvl", 0, f_tvl},
  {"crt_scanlines", 1, f_scanlines}, {"crt_mask", 1, f_mask}, {"crt_glow", 1, f_glow},
  {"crt_curvature", 1, f_curvature}, {"crt_vignette", 1, f_vignette}, {"crt_sharpness", 1, f_sharpness},
  {"crt_brightness", 1, f_brightness},
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void settings_load(AppSettings *s, const char *path)
{
  settings_default(s);
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char key[64];
    char value[64];
    if (sscanf(line, " %63[a-z0-9_] = %63s", key, value) != 2)
      continue;
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++)
      if (!strcmp(entries[i].key, key)) {
        if (entries[i].is_float) *(float *)entries[i].field(s) = (float)atof(value);
        else *(int *)entries[i].field(s) = atoi(value);
      }
  }
  fclose(f);
  s->window_preset = clampi(s->window_preset, 0, window_preset_count - 1);
  s->render_index = clampi(s->render_index, 0, render_height_count - 1);
  s->vsync = clampi(s->vsync, 0, VSYNC_COUNT - 1);
  if (s->fps_limit != GAME_FPS_120)
    s->fps_limit = GAME_FPS_60;
  s->volume = clampi(s->volume, 0, 100);
  s->music_track = clampi(s->music_track, 0, MUSIC_TRACK_COUNT - 1);
  s->video.scale_mode = clampi(s->video.scale_mode, 0, SCALE_COUNT - 1);
  s->video.crt = clampi(s->video.crt, 0, CRT_COUNT - 1);
  s->video.screen_format = clampi(s->video.screen_format, 0, FORMAT_COUNT - 1);
  if (s->video.mask_tvl != 450 && s->video.mask_tvl != 600 && s->video.mask_tvl != 750 && s->video.mask_tvl != 900)
    s->video.mask_tvl = 0;
}

void settings_save(const AppSettings *s, const char *path)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "# Super Hang-On PC port settings (written by the settings menu)\n");
  AppSettings copy = *s;
  for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
    if (entries[i].is_float)
      fprintf(f, "%s = %.2f\n", entries[i].key, *(float *)entries[i].field(&copy));
    else
      fprintf(f, "%s = %d\n", entries[i].key, *(int *)entries[i].field(&copy));
  }
  fclose(f);
}
