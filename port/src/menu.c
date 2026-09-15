#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "ui.h"

enum { KIND_ACTION, KIND_HEADER, KIND_CHOICE, KIND_PERCENT };

typedef struct {
  int kind;
  const char *label;
  int id;
} Item;

enum {
  ID_RESUME, ID_RESET, ID_QUIT, ID_DISPLAY, ID_WINDOW, ID_RENDER, ID_SCALING, ID_ASPECT, ID_FORMAT, ID_VSYNC, ID_RATE, ID_FPS,
  ID_FILTER, ID_TVL, ID_SCANLINES, ID_MASK, ID_GLOW, ID_CURVATURE, ID_VIGNETTE, ID_SHARPNESS, ID_BRIGHTNESS,
  ID_VOLUME, ID_NONE
};

static const Item items[] = {
  {KIND_ACTION, "RESUME", ID_RESUME},
  {KIND_HEADER, "VIDEO", ID_NONE},
  {KIND_CHOICE, "DISPLAY", ID_DISPLAY},
  {KIND_CHOICE, "WINDOW SIZE", ID_WINDOW},
  {KIND_CHOICE, "RENDER RESOLUTION", ID_RENDER},
  {KIND_CHOICE, "SCALING", ID_SCALING},
  {KIND_CHOICE, "ASPECT RATIO", ID_ASPECT},
  {KIND_CHOICE, "SCREEN FORMAT", ID_FORMAT},
  {KIND_CHOICE, "V-SYNC", ID_VSYNC},
  {KIND_CHOICE, "FRAME RATE", ID_RATE},
  {KIND_CHOICE, "FPS COUNTER", ID_FPS},
  {KIND_HEADER, "CRT", ID_NONE},
  {KIND_CHOICE, "FILTER", ID_FILTER},
  {KIND_PERCENT, "SCANLINES", ID_SCANLINES},
  {KIND_PERCENT, "PHOSPHOR MASK", ID_MASK},
  {KIND_CHOICE, "MASK TVL", ID_TVL},
  {KIND_PERCENT, "GLOW", ID_GLOW},
  {KIND_PERCENT, "CURVATURE", ID_CURVATURE},
  {KIND_PERCENT, "VIGNETTE", ID_VIGNETTE},
  {KIND_PERCENT, "SHARPNESS", ID_SHARPNESS},
  {KIND_PERCENT, "BRIGHTNESS", ID_BRIGHTNESS},
  {KIND_HEADER, "AUDIO", ID_NONE},
  {KIND_PERCENT, "VOLUME", ID_VOLUME},
  {KIND_ACTION, "RESET GAME", ID_RESET},
  {KIND_ACTION, "QUIT GAME", ID_QUIT},
};
#define N_ITEMS (int)(sizeof items / sizeof items[0])

int menu_rate = 1;
int menu_rate_choice;

static int cursor;
static uint16_t prev_pad;
static int repeat_timer;

void menu_open(void)
{
  cursor = 0;
  prev_pad = 0xff;                   /* the button that opened the menu must be released first */
  repeat_timer = 0;
}

static float *percent_field(AppSettings *s, int id, float *lo, float *hi)
{
  *lo = 0.0f;
  *hi = 1.0f;
  switch (id) {
    case ID_SCANLINES: return &s->video.scanlines;
    case ID_MASK: return &s->video.mask_strength;
    case ID_GLOW: return &s->video.glow;
    case ID_CURVATURE: return &s->video.curvature;
    case ID_VIGNETTE: return &s->video.vignette;
    case ID_SHARPNESS: return &s->video.sharpness;
    case ID_BRIGHTNESS: *lo = 0.5f; *hi = 2.0f; return &s->video.brightness;
    default: return NULL;
  }
}

static int wrap(int v, int n)
{
  return (v % n + n) % n;
}

/* changes the item by dir (-1 / +1); returns APPLY_* */
static int change(AppSettings *s, int id, int dir)
{
  switch (id) {
    case ID_DISPLAY: s->fullscreen = !s->fullscreen; return APPLY_FULLSCREEN;
    case ID_WINDOW: s->window_preset = wrap(s->window_preset + dir, window_preset_count); return APPLY_WINDOW;
    case ID_RENDER: s->render_index = wrap(s->render_index + dir, render_height_count); return 0;
    case ID_SCALING: s->video.scale_mode = wrap(s->video.scale_mode + dir, SCALE_COUNT); return 0;
    case ID_ASPECT: s->video.aspect_43 = !s->video.aspect_43; return APPLY_WINDOW;
    case ID_FORMAT: s->video.screen_format = wrap(s->video.screen_format + dir, FORMAT_COUNT); return 0;
    case ID_VSYNC: s->vsync = wrap(s->vsync + dir, VSYNC_COUNT); return APPLY_VSYNC;
    case ID_RATE:
      if (menu_rate_choice) s->frame_rate = s->frame_rate == 120 ? 60 : 120;
      return 0;
    case ID_FPS: s->show_fps = !s->show_fps; return 0;
    case ID_FILTER: s->video.crt = wrap(s->video.crt + dir, CRT_COUNT); return 0;
    case ID_TVL: {
      static const int tvl[] = {0, 450, 600, 750, 900};
      int k = 0;
      for (int i = 0; i < 5; i++) if (tvl[i] == s->video.mask_tvl) k = i;
      s->video.mask_tvl = tvl[wrap(k + dir, 5)];
      return 0;
    }
    case ID_VOLUME: {
      int v = s->volume + dir * 5;
      s->volume = v < 0 ? 0 : v > 100 ? 100 : v;
      return 0;
    }
    default: {
      float lo, hi;
      float *f = percent_field(s, id, &lo, &hi);
      if (f) {
        float v = *f + dir * (hi - lo) / 20.0f;
        *f = v < lo ? lo : v > hi ? hi : v;
      }
      return 0;
    }
  }
}

int menu_update(AppSettings *s, uint16_t pad, int back, int *apply)
{
  uint16_t pressed = pad & ~prev_pad;
  /* auto-repeat of held directions */
  if (pad & 0x0f) {
    if (++repeat_timer > 18 * menu_rate && repeat_timer % (4 * menu_rate) == 0)
      pressed |= pad & 0x0f;
  } else {
    repeat_timer = 0;
  }
  prev_pad = pad;
  *apply = 0;

  if (back)
    return MENU_CLOSED;
  if (pressed & 0x01)
    do cursor = wrap(cursor - 1, N_ITEMS); while (items[cursor].kind == KIND_HEADER);
  if (pressed & 0x02)
    do cursor = wrap(cursor + 1, N_ITEMS); while (items[cursor].kind == KIND_HEADER);
  const Item *it = &items[cursor];
  if (it->kind == KIND_ACTION && (pressed & 0xf0)) {
    if (it->id == ID_QUIT)
      return MENU_QUIT;
    if (it->id == ID_RESET)
      return MENU_RESET;
    return MENU_CLOSED;
  }
  int dir = (pressed & 0x04) ? -1 : (pressed & 0x08) ? 1 : (pressed & 0xd0) ? 1 : 0;
  if (dir && (it->kind == KIND_CHOICE || it->kind == KIND_PERCENT))
    *apply |= change(s, it->id, dir) | APPLY_SAVE;
  return MENU_OPEN;
}

static const char *value_text(const AppSettings *s, int id, char *buf, size_t n)
{
  static const char *scaling[] = {"INTEGER", "FIT", "STRETCH"};
  static const char *filters[] = {"OFF", "SCANLINES", "APERTURE GRILLE", "SLOT MASK", "SHADOW MASK"};
  static const char *vsync[] = {"AUTO", "ON", "OFF"};
  switch (id) {
    case ID_DISPLAY: return s->fullscreen ? "FULLSCREEN" : "WINDOW";
    case ID_WINDOW: return window_presets[s->window_preset].label;
    case ID_RENDER:
      if (!render_heights[s->render_index]) return "AS WINDOW";
      snprintf(buf, n, "%d LINES", render_heights[s->render_index]);
      return buf;
    case ID_SCALING: return scaling[s->video.scale_mode];
    case ID_ASPECT: return s->video.aspect_43 ? "TV PIXELS" : "SQUARE PIXELS";
    case ID_FORMAT: {
      static const char *formats[] = {"4:3", "16:9", "21:9"};
      return formats[s->video.screen_format];
    }
    case ID_VSYNC: return vsync[s->vsync];
    case ID_RATE:
      if (s->frame_rate != 60 * menu_rate) {
        snprintf(buf, n, "%d FPS (RESTART)", s->frame_rate);
        return buf;
      }
      return s->frame_rate == 120 ? "120 FPS" : "60 FPS";
    case ID_FPS: return s->show_fps ? "ON" : "OFF";
    case ID_FILTER: return filters[s->video.crt];
    case ID_TVL:
      if (!s->video.mask_tvl) return "AUTO";
      snprintf(buf, n, "%d TVL", s->video.mask_tvl);
      return buf;
    case ID_VOLUME: snprintf(buf, n, "%d%%", s->volume); return buf;
    default: {
      float lo, hi;
      AppSettings copy = *s;
      float *f = percent_field(&copy, id, &lo, &hi);
      if (!f) return "";
      snprintf(buf, n, "%d%%", (int)(*f * 100.0f + 0.5f));
      return buf;
    }
  }
}

void menu_draw(const AppSettings *s)
{
  const int scale = 2, row_h = 16;
  const int panel_w = 520, panel_h = 60 + N_ITEMS * row_h;
  const int px = (UI_W - panel_w) / 2, py = (UI_H - panel_h) / 2;
  ui_box(0, 0, UI_W, UI_H, 0x80000000);                            /* dim the game */
  ui_box(px, py, panel_w, panel_h, 0xe0101828);
  ui_box(px, py, panel_w, 3, 0xffffc020);
  ui_box(px, py + panel_h - 3, panel_w, 3, 0xffffc020);
  const char *title = "SUPER HANG-ON  SETTINGS";
  ui_text((UI_W - ui_text_width(scale, title)) / 2, py + 14, scale, 0xffffe060, title);
  int crt_off = s->video.crt == CRT_OFF;
  for (int i = 0; i < N_ITEMS; i++) {
    const Item *it = &items[i];
    int y = py + 44 + i * row_h;
    if (it->kind == KIND_HEADER) {
      ui_text(px + 24, y, scale, 0xff60c0ff, it->label);
      ui_box(px + 24 + ui_text_width(scale, it->label) + 10, y + 6, panel_w - 72 - ui_text_width(scale, it->label), 2,
             0xff305070);
      continue;
    }
    int selected = i == cursor;
    int dim = (crt_off && it->id == ID_RENDER) || (crt_off && (it->kind == KIND_PERCENT || it->id == ID_TVL) && it->id != ID_VOLUME) ||
              (it->id == ID_TVL && s->video.crt < CRT_APERTURE_GRILLE) || (it->id == ID_RATE && !menu_rate_choice);
    if (selected)
      ui_box(px + 12, y - 4, panel_w - 24, row_h - 1, 0xff304868);
    uint32_t color = dim ? 0xff707880 : selected ? 0xffffffff : 0xffc8d0d8;
    ui_text(px + 40, y, scale, color, it->label);
    if (it->kind == KIND_CHOICE || it->kind == KIND_PERCENT) {
      char buf[32], line[48];
      const char *v = value_text(s, it->id, buf, sizeof buf);
      snprintf(line, sizeof line, selected ? "< %s >" : "%s", v);
      int w = ui_text_width(scale, line);
      ui_text(px + panel_w - 40 - w + (selected ? 0 : -24), y, scale, selected ? 0xffffe060 : color, line);
    }
  }
}
