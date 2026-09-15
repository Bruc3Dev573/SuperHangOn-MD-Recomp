/*
 * Super Hang-On PC port: SDL2 frontend.
 *
 *   shangon [--rom PATH] [--scale N] [--window W H] [--fullscreen] [--vsync | --no-vsync]
 *           [--crt off|scanlines|aperture|slot|shadow] [--render-height N]
 *           [--scale-mode integer|fit|stretch] [--square-pixels] [--format 4:3|16:9|21:9]
 *           [--no-gl] [--no-fps]
 *           [--mute] [--frames N] [--input FILE] [--screenshot FRAME[-LAST] FILE]
 *           [--no-rom-check] [--data DIR] [--fps 60|120] [--ram-log FILE EVERY]
 *
 * The ROM must be the one the game code was recompiled from (SHA1 checked).
 * Default ROM: baserom.md in the game directory, else rom/baserom.md (a
 * build run from the repository). The game directory is the directory of the
 * executable; it also holds settings.ini, controls.ini, the save state slots and
 * records.bin (--data DIR puts those elsewhere).
 *
 * --frames quits after N frames and prints the rate; --ram-log writes
 * "frame rate time speed distance lateral score stage" every EVERY frames
 * (tests); --input adds scripted presses, lines "first_frame last_frame BUTTONS" (pad: U D L R A B C S;
 * hotkeys: 5 save state, 8 load state, W rewind, M settings menu, F frame
 * rate 60 / 120, Q reset the game).
 *
 * The recompiled game runs from the reset vector (rt_start); at every video
 * frame the runtime calls on_frame(), which renders the VDP state, presents
 * it, queues the frame's sound, polls input and paces the loop to 60 frames
 * per second. The sound is resampled from the native YM2612 rate to the device
 * rate; the ratio follows the device queue level so that sound and video,
 * driven by different clocks, stay in step without gaps or growing latency.
 *
 * Controls are configured in controls.ini (written with the defaults on the
 * first run): arrows / D-pad / left stick, Z X C = A B C (controller X A B and
 * the triggers: right = B accelerate, left = A brake), Enter / Start; F5 save
 * state, F8 load, F6 / F7 slot, Backspace / left shoulder rewind, F11
 * fullscreen, F3 frame rate counter, Esc / F1 / controller Back settings menu.
 *
 * Display, CRT filter and audio settings are kept in settings.ini (settings
 * menu); command line options override them for one run.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "recomp_rt.h"
#include "scene.h"
#include "md.h"
#include "render.h"
#include "audio.h"
#include "resample.h"
#include "input.h"
#include "persist.h"
#include "sha1.h"
#include "rompatch.h"
#ifdef RT_TRANSLATE
#include "rt_translate.h"
#ifdef RT_CODE_SETS_RATES
extern const RtCodeSet rt_code_set_60hz, rt_code_set_120hz;
#else
extern const RtCodeSet rt_code_set_original;
#endif
#endif
#include "overlay.h"
#include "png.h"
#include "video.h"
#include "settings.h"
#include "ui.h"
#include "menu.h"

static int FPS = 60;                  /* video frames per second: 60 x rt_rate */
#define ROM_SHA1 "ecfd7b3bf4dcbee472ddf2f9cdbe968a05b814e0"

static SDL_Window *window;
static SDL_Renderer *renderer;       /* fallback when OpenGL is not available */
static int use_gl;
static AppSettings settings;
static char settings_path[1100];
static int cli_vsync = -1;           /* --vsync / --no-vsync */
static int refresh_hz;
static SDL_Texture *texture;
static int texture_w;
static int use_vsync;
static Uint64 next_frame;
static Uint64 perf_freq;
static long frame_count, frame_limit;
static SDL_AudioDeviceID audio_dev;
static int audio_rate;
static Resampler resampler;
static double audio_step;             /* nominal input frames per output frame */
static Uint64 start_time;
#ifdef RT_CODE_SETS_RATES
static uint8_t *rom_clean;            /* the ROM without the overlays of a rate */
static char data_path[1024];          /* where the code caches are kept */
#endif

#define SCRIPT_SAVE 0x100
#define SCRIPT_LOAD 0x200
#define SCRIPT_REWIND 0x400
#define SCRIPT_MENU 0x800
#define SCRIPT_RATE 0x1000
#define SCRIPT_RESET 0x2000
static struct { long first, last; uint16_t mask; } script[256];
static int script_len;

static void load_script(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); exit(1); }
  char line[128], btn[32];
  long a, b;
  while (script_len < 256 && fgets(line, sizeof line, f)) {
    if (sscanf(line, "%ld %ld %31s", &a, &b, btn) != 3)
      continue;
    uint16_t m = 0;
    for (char *p = btn; *p; p++)
      m |= *p == 'U' ? 0x01 : *p == 'D' ? 0x02 : *p == 'L' ? 0x04 : *p == 'R' ? 0x08 :
           *p == 'B' ? 0x10 : *p == 'C' ? 0x20 : *p == 'A' ? 0x40 : *p == 'S' ? 0x80 :
           *p == '5' ? SCRIPT_SAVE : *p == '8' ? SCRIPT_LOAD : *p == 'W' ? SCRIPT_REWIND :
           *p == 'M' ? SCRIPT_MENU : *p == 'F' ? SCRIPT_RATE : *p == 'Q' ? SCRIPT_RESET : 0;
    script[script_len].first = a;
    script[script_len].last = b;
    script[script_len++].mask = m;
  }
  fclose(f);
}

static uint16_t script_buttons(void)
{
  uint16_t m = 0;
  for (int i = 0; i < script_len; i++)
    if (frame_count >= script[i].first && frame_count <= script[i].last)
      m |= script[i].mask;
  return m;
}

static void apply_window(void)
{
  if (settings.fullscreen)
    return;
  const WindowPreset *p = &window_presets[settings.window_preset];
  int w = p->w, h = p->h;
  SDL_Rect usable;
  if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(window), &usable) == 0) {
    if (w > usable.w) { h = h * usable.w / w; w = usable.w; }
    if (h > usable.h) { w = w * usable.h / h; h = usable.h; }
  }
  SDL_SetWindowSize(window, w, h);
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static void apply_fullscreen(void)
{
  SDL_SetWindowFullscreen(window, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (!settings.fullscreen)
    apply_window();
}

static void apply_vsync(void)
{
  use_vsync = cli_vsync >= 0 ? cli_vsync :
              settings.vsync == VSYNC_ON ? 1 : settings.vsync == VSYNC_OFF ? 0 : refresh_hz == FPS;
  if (use_gl)
    SDL_GL_SetSwapInterval(use_vsync);
}

static void toggle_fullscreen(void)
{
  settings.fullscreen = !settings.fullscreen;
  apply_fullscreen();
  settings_save(&settings, settings_path);
}

static int req_save, req_load, rewinding, menu_request;
static Uint64 title_until;

static void show_message(const char *text)
{
  char title[128];
  snprintf(title, sizeof title, text ? "Super Hang-On - %s" : "Super Hang-On", text);
  SDL_SetWindowTitle(window, title);
  title_until = text ? SDL_GetPerformanceCounter() + perf_freq * 2 : 0;
}

static void poll_events(void)
{
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT)
      exit(0);
    input_event(&e);
  }
  if (input_hotkey_pressed(HOTKEY_MENU) | input_hotkey_pressed(HOTKEY_QUIT))
    menu_request = 1;
  if (input_hotkey_pressed(HOTKEY_FULLSCREEN))
    toggle_fullscreen();
  if (input_hotkey_pressed(HOTKEY_SLOT_PREV))
    persist_set_slot(persist_slot() - 1);
  if (input_hotkey_pressed(HOTKEY_SLOT_NEXT))
    persist_set_slot(persist_slot() + 1);
  uint16_t scripted = script_buttons();
  static uint16_t scripted_prev;
  req_save |= input_hotkey_pressed(HOTKEY_SAVE) || (scripted & ~scripted_prev & SCRIPT_SAVE);
  req_load |= input_hotkey_pressed(HOTKEY_LOAD) || (scripted & ~scripted_prev & SCRIPT_LOAD);
  rewinding = input_hotkey_held(HOTKEY_REWIND) || (scripted & SCRIPT_REWIND);
  if (scripted & ~scripted_prev & SCRIPT_MENU)
    menu_request = 1;
  if (scripted & ~scripted_prev & SCRIPT_RESET)
    persist_request_reset();
  if (scripted & ~scripted_prev & SCRIPT_RATE)
    settings.frame_rate = settings.frame_rate == 120 ? 60 : 120;   /* scripted runs: change the rate */
  scripted_prev = scripted;
  md.pad_buttons[0] = input_pad() | (scripted & 0xff);
  /* analog steering for the race (patches/pc60/controls.asm) */
  AnalogInput analog;
  input_analog(&analog);
  md.ram[0xc640] = analog.flags;
  md.ram[0xc643] = (uint8_t)analog.steer;

  const char *msg = persist_message();
  if (msg) {
    show_message(msg);
    if (script_len)
      printf("frame %ld: %s\n", frame_count, msg);
  }
  else if (title_until && SDL_GetPerformanceCounter() > title_until)
    show_message(NULL);
}

#ifdef RT_CODE_SETS_RATES
static int rate_apply(int rate);
static void rate_scale_timers(int previous, int rate);
#endif

static void on_frame_end(M68K *c, uint32_t resume_pc)
{
  (void)resume_pc;
  int save = req_save, load = req_load;
  req_save = req_load = 0;
  persist_frame_end(c, save, load, rewinding);
  scene_frame_end();
#ifdef RT_CODE_SETS_RATES
  /* FRAME RATE was changed in the settings menu: the game code of the other
   * rate takes over between two frames, with the machine as it is (the two
   * builds have the same blocks at the same addresses). Not before the game
   * has initialised: the boot sums the whole ROM and compares it with the
   * checksum in its header, and a sum of two different images fails (the
   * game then shows its red error screen). */
  int want = settings.frame_rate == 120 ? 2 : 1;
  if (want != rt_rate && !memcmp(md.ram, "init", 4)) {
    int previous = rt_rate;
    if (rate_apply(want) != 0 && rate_apply(previous) != 0) {
      fprintf(stderr, "shangon: the game code of the frame rate could not be decoded\n");
      exit(3);
    }
    settings.frame_rate = 60 * rt_rate;
    if (rt_rate != previous)
      rate_scale_timers(previous, rt_rate);
    persist_rate_changed();
    rt_resume_at(c, resume_pc);                 /* does not return */
  }
#endif
}

static Scene scene;
static int scene_valid, scene_age = 1000;       /* frames since the last wide picture */

static void present(void)
{
  int w = render_width;
  if (use_gl) {
    settings.video.render_height = render_heights[settings.render_index];
    VideoWide wide = {scene_valid ? &scene : NULL, render_wide_back[0], render_wide_plane_lo[0], render_wide_plane_hi[0]};
    /* a race frame the scene could not be built for shows the last wide
     * picture again; others are 4:3 */
    int shown = scene_valid || scene_age <= 3;
    video_present(render_frame_rgb[0], w, MD_MAX_H, MD_MAX_W, &settings.video, shown ? &wide : NULL,
                  ui_canvas[0], UI_W, UI_H);
    return;
  }
  if (w != texture_w) {
    if (texture)
      SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, MD_MAX_H);
    SDL_RenderSetLogicalSize(renderer, w, MD_MAX_H);
    texture_w = w;
  }
  void *pixels;
  int pitch;
  if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0) {
    for (int y = 0; y < MD_MAX_H; y++)
      memcpy((uint8_t *)pixels + y * pitch, render_frame_rgb[y], (size_t)w * 4);
    SDL_UnlockTexture(texture);
  }
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

/* without vsync: sleep until the next 1/60 s boundary (coarse sleep, then spin) */
static void pace(void)
{
  Uint64 period = perf_freq / FPS;
  Uint64 now = SDL_GetPerformanceCounter();
  if (!next_frame || now > next_frame + period * 4)
    next_frame = now;                                 /* start, or fell far behind */
  next_frame += period;
  for (;;) {
    now = SDL_GetPerformanceCounter();
    if (now >= next_frame)
      break;
    Uint64 left_ms = (next_frame - now) * 1000 / perf_freq;
    if (left_ms > 2)
      SDL_Delay((Uint32)(left_ms - 2));
  }
}

#define AUDIO_LATENCY 0.06            /* target queue, seconds */

static void open_audio(void)
{
  SDL_AudioSpec want = {0}, have;
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!audio_dev) {
    fprintf(stderr, "shangon: no audio (%s)\n", SDL_GetError());
    return;
  }
  audio_rate = have.freq;
  /* the emulated frame (262 lines) is played in one 1/60 s video frame */
  double in_per_second = (double)(MD_MCYCLES_PER_LINE * MD_LINES_PER_FRAME) / 1008.0 * 60;
  audio_step = in_per_second / audio_rate;
  resample_init(&resampler, in_per_second, audio_rate);
  SDL_PauseAudioDevice(audio_dev, 0);
}

static void queue_audio(void)
{
  static int16_t native[4096 * 2], out[4096 * 2];
  size_t n = audio_read(native, 4096);
  if (!audio_dev || rewinding)
    return;
  if (settings.volume < 100)
    for (size_t i = 0; i < n * 2; i++)
      native[i] = (int16_t)(native[i] * settings.volume / 100);
  /* proportional rate control on the smoothed queue level: 1% per target
   * latency of error, at most 2% */
  static double level = -1;
  double target = audio_rate * AUDIO_LATENCY;
  double queued = SDL_GetQueuedAudioSize(audio_dev) / 4.0;
  if (queued > audio_rate * 0.3) {                  /* stalled (window moved, suspend...) */
    SDL_ClearQueuedAudio(audio_dev);
    queued = 0;
    level = -1;
  }
  level = level < 0 ? queued : level * 0.95 + queued * 0.05;
  double adjust = 0.01 * (level - target) / target;
  if (adjust > 0.02) adjust = 0.02;
  if (adjust < -0.02) adjust = -0.02;
  resample_set_step(&resampler, audio_step * (1 + adjust));
  size_t m = resample_run(&resampler, native, n, out, 4096);
  SDL_QueueAudio(audio_dev, out, (Uint32)(m * 4));
}

/* frame rate counter (F3): frames shown in the last second, frames that came
 * more than 25 ms after the previous one (since start), the longest frame
 * interval and the longest emulation time of the last second */
static long shot_frame = -1, shot_last = -1;   /* --screenshot FRAME[-LAST] FILE (tests) */
static const char *shot_file;
static FILE *ram_log;                          /* --ram-log FILE EVERY (tests) */
static long ram_log_every;

/* a few game variables per frame, to follow a race from outside */
static void ram_log_frame(void)
{
  if (!ram_log || frame_count % ram_log_every)
    return;
  const uint8_t *r = md.ram;
  unsigned speed = (unsigned)(r[0x64a] << 24 | r[0x64b] << 16 | r[0x64c] << 8 | r[0x64d]);
  unsigned dist = (unsigned)(r[0x616] << 24 | r[0x617] << 16 | r[0x618] << 8 | r[0x619]);
  int lateral = (int16_t)(r[0x614] << 8 | r[0x615]);
  fprintf(ram_log, "%ld %d %02x %u %u %d %02x%02x%02x%02x %d\n", frame_count, rt_rate, r[0x554],
          speed, dist, lateral, r[0x54c], r[0x54d], r[0x54e], r[0x54f],
          (int)(r[0x52c] << 8 | r[0x52d]));
}

static int shot_wanted(void)
{
  return shot_frame >= 0 && frame_count >= shot_frame && frame_count <= shot_last;
}

/* FILE may contain %ld for the frame number */
static void write_shot(const uint32_t *pixels, int w, int h, int stride)
{
  char path[1024];
  snprintf(path, sizeof path, shot_file, frame_count);
  png_write_rgb32(path, pixels, w, h, stride);
}
static struct {
  Uint64 last_frame, window_start, emu_start;
  int frames, late_total;
  double max_interval_ms, max_emu_ms;
  char text[64];
} fps;

static void fps_update(Uint64 frame_start)
{
  double ms = 1000.0 / (double)perf_freq;
  if (fps.last_frame) {
    double interval = (double)(frame_start - fps.last_frame) * ms;
    if (interval > 25.0)
      fps.late_total++;
    if (interval > fps.max_interval_ms)
      fps.max_interval_ms = interval;
  }
  if (fps.emu_start) {
    double emu = (double)(frame_start - fps.emu_start) * ms;
    if (emu > fps.max_emu_ms)
      fps.max_emu_ms = emu;
  }
  fps.last_frame = frame_start;
  fps.frames++;
  if (!fps.window_start)
    fps.window_start = frame_start;
  double window = (double)(frame_start - fps.window_start) * ms;
  if (window >= 1000.0) {
    char sync[16];
    if (use_vsync) snprintf(sync, sizeof sync, "VSYNC %d", refresh_hz);
    else snprintf(sync, sizeof sync, "TIMER %d", refresh_hz);
    snprintf(fps.text, sizeof fps.text, "%.1f FPS LATE %d MAX %.0f CPU %.1f %s",
             (fps.frames - 1) * 1000.0 / window, fps.late_total, fps.max_interval_ms, fps.max_emu_ms, sync);
    fps.window_start = frame_start;
    fps.frames = 1;
    fps.max_interval_ms = 0;
    fps.max_emu_ms = 0;
  }
}

#ifdef RT_CODE_SETS_RATES
/* The game counts the seconds of a race in ticks: the length of a second is
 * kept in its RAM ($FF0556, written at the start of a race by
 * patches/pc60/timer.asm) and counted down at $FF0555 (the race clock) and at
 * $FFC88C (the time of the ranking). With the new rate a second is a different
 * number of ticks, so the length is rewritten and what is left of the current
 * second is rescaled; without this the clock of a race that had already
 * started ran at half or twice its speed. */
static void rate_scale_timers(int previous, int rate)
{
  unsigned second = 60u * (unsigned)rate;
  md.ram[0x556] = (uint8_t)second;
  static const unsigned counters[] = {0x555, 0xc88c};
  for (unsigned i = 0; i < sizeof counters / sizeof counters[0]; i++) {
    unsigned left = md.ram[counters[i]];
    if (!left)
      continue;
    left = left * (unsigned)rate / (unsigned)previous;
    if (left < 1) left = 1;
    if (left > second) left = second;
    md.ram[counters[i]] = (uint8_t)left;
  }
  /* the phases of the overlays (patches/pc60/00_sched_ram.asm) count the ticks
   * of a 30 Hz tick and act on the last one: what is left of the phase is
   * rescaled too, so that steering, throttle and the steps of everything that
   * moves once per 30 Hz tick keep their place in the tick */
  unsigned ticks_old = 2u * (unsigned)previous, ticks_new = 2u * (unsigned)rate;
  static const unsigned phases[] = {0xc621, 0xc628, 0xc629};
  for (unsigned i = 0; i < sizeof phases / sizeof phases[0]; i++) {
    unsigned p = md.ram[phases[i]] & (ticks_old - 1);
    unsigned left = (ticks_old - 1 - p) * ticks_new / ticks_old;
    md.ram[phases[i]] = (uint8_t)(ticks_new - 1 - left);
  }
}

/* Puts the game code of `rate` (60 or 120 logic ticks per second) in place:
 * the overlay bytes of that build are applied to a clean copy of the ROM and
 * the code is decoded again (a few thousand instructions, or read from the
 * rate's cache file). The game state is untouched, so this also works between
 * two frames of a race. Returns -1 when the code could not be decoded. */
static int rate_apply(int rate)
{
  const RtCodeSet *set = rate == 2 ? &rt_code_set_120hz : &rt_code_set_60hz;
  memcpy(md.rom, rom_clean, md.rom_size);
  const RomPatch *patches = set->patches;
  for (int i = 0; i < *set->patch_count; i++)
    if (patches[i].addr + patches[i].len <= md.rom_size)
      memcpy(md.rom + patches[i].addr, patches[i].bytes, patches[i].len);
  char cache[1100];
  snprintf(cache, sizeof cache, rate == 2 ? "%sshangon120.cache" : "%sshangon.cache", data_path);
  if (rt_translate_init(set, md.rom, md.rom_size, cache) != 0)
    return -1;
  rt_set_rate(rate);
  FPS = 60 * rate;
  menu_rate = rate;
  apply_vsync();                      /* the automatic choice follows the frame rate */
  next_frame = 0;                     /* the frame period changed: pace from now */
  fps.last_frame = 0;
  return 0;
}
#endif

/* the settings menu runs its own loop: the game is paused meanwhile */
static void run_menu(void)
{
  menu_open();
  if (audio_dev)
    SDL_ClearQueuedAudio(audio_dev);
  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        settings_save(&settings, settings_path);
        exit(0);
      }
      input_event(&e);
    }
    static uint16_t scripted_prev = SCRIPT_MENU;
    uint16_t scripted = script_buttons();
    int back = input_hotkey_pressed(HOTKEY_MENU) | input_hotkey_pressed(HOTKEY_QUIT) |
               !!(scripted & ~scripted_prev & SCRIPT_MENU);
    scripted_prev = scripted;
    int apply = 0;
    int state = menu_update(&settings, input_pad() | (script_buttons() & 0xff), back, &apply);
    if (apply & APPLY_FULLSCREEN) apply_fullscreen();
    if (apply & APPLY_WINDOW) apply_window();
    if (apply & APPLY_VSYNC) apply_vsync();
    if (apply & APPLY_SAVE) settings_save(&settings, settings_path);
    if (state == MENU_QUIT) {
      settings_save(&settings, settings_path);
      exit(0);
    }
    if (state == MENU_RESET) {
      persist_request_reset();
      break;
    }
    if (state == MENU_CLOSED)
      break;
    ui_clear();
    menu_draw(&settings);
    if (shot_wanted())
      video_capture_next();
    present();
    int cw, ch;
    const uint32_t *cap = video_captured(&cw, &ch);
    if (cap)
      write_shot(cap, cw, ch, cw);
    if (!use_vsync)
      pace();
    if (script_len)
      frame_count++;                              /* scripted runs keep counting frames */
    if (frame_limit && frame_count >= frame_limit)
      exit(0);
  }
  /* avoid a frame rate hiccup on the counter */
  fps.last_frame = 0;
}

static void on_frame(M68K *c)
{
  (void)c;
  Uint64 frame_start = SDL_GetPerformanceCounter();
  fps_update(frame_start);
  render_frame();
  /* wide screen formats: the race picture is rebuilt wider */
  int ext = use_gl && render_width == 320 ? video_wide_ext(&settings.video) : 0;
  if (ext > 0)
    render_frame_wide(ext);
  scene_valid = ext > 0 && scene_build(&scene, ext);
  scene_age = scene_valid ? 0 : scene_age + 1;
  if (use_gl) {
    ui_clear();
    if (settings.show_fps && fps.text[0]) {
      ui_box(2, 2, ui_text_width(1, fps.text) + 4, 11, 0xc0000000);
      ui_text(4, 4, 1, 0xffffff40, fps.text);
    }
    if (shot_wanted())
      video_capture_next();
  } else if (settings.show_fps && fps.text[0]) {
    overlay_text(render_frame_rgb, render_width, MD_MAX_H, 2, 2, fps.text);
  }
  if (!use_gl && shot_wanted())
    write_shot(render_frame_rgb[0], render_width, MD_MAX_H, MD_MAX_W);
  present();
  if (use_gl) {
    int cw, ch;
    const uint32_t *cap = video_captured(&cw, &ch);
    if (cap)
      write_shot(cap, cw, ch, cw);
  }
  ram_log_frame();
  queue_audio();
  if (!use_vsync)
    pace();
  poll_events();
  if (input_hotkey_pressed(HOTKEY_FPS)) {
    settings.show_fps = !settings.show_fps;
    settings_save(&settings, settings_path);
  }
  if (menu_request && use_gl)
    run_menu();
  menu_request = 0;
  fps.emu_start = SDL_GetPerformanceCounter();
  if (++frame_count >= frame_limit && frame_limit) {
    double secs = (double)(SDL_GetPerformanceCounter() - start_time) / (double)perf_freq;
    printf("%ld frames in %.3f s: %.3f fps\n", frame_count, secs, frame_count / secs);
    exit(0);
  }
}

/* fatal start-up error: stderr and a message box (no console on Windows) */
static int fatal(const char *fmt, ...)
{
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  fprintf(stderr, "shangon: %s\n", msg);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Hang-On", msg, window);
  return 1;
}

static void usage(void)
{
  fprintf(stderr, "usage: shangon [--rom PATH] [--scale N] [--window W H] [--fullscreen] [--vsync | --no-vsync]\n"
                  "               [--crt off|scanlines|aperture|slot|shadow] [--render-height N]\n"
                  "               [--scale-mode integer|fit|stretch] [--square-pixels] [--no-gl] [--no-fps]\n"
                  "               [--mute] [--frames N] [--input FILE] [--screenshot FRAME FILE]\n"
                  "               [--no-rom-check] [--data DIR] [--fps 60|120]\n");
  exit(1);
}

/* the directory of the executable, with a trailing separator */
static void game_directory(char *out, size_t size)
{
  out[0] = 0;
  char *base = SDL_GetBasePath();
  if (!base)
    return;
  snprintf(out, size, "%s", base);
  SDL_free(base);
}

int main(int argc, char **argv)
{
  const char *rom = NULL, *data_dir = NULL;
  int cli_fps = 0;
  int no_gl = 0, win_w = 0, win_h = 0;
  int scale = 0, fullscreen = 0, mute = 0, rom_check = 1;
  /* data directory first: settings.ini lives there, the options override it */
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], "--data"))
      data_dir = argv[i + 1];
  char data[1024];
  if (data_dir) {
    size_t n = strlen(data_dir);
    snprintf(data, sizeof data, "%s%s", data_dir, n && (data_dir[n - 1] == '/' || data_dir[n - 1] == '\\') ? "" : "/");
  } else {
    game_directory(data, sizeof data);
  }
  snprintf(settings_path, sizeof settings_path, "%ssettings.ini", data);
  settings_load(&settings, settings_path);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
    else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
    else if (!strcmp(argv[i], "--vsync")) cli_vsync = 1;
    else if (!strcmp(argv[i], "--no-vsync")) cli_vsync = 0;
    else if (!strcmp(argv[i], "--mute")) mute = 1;
    else if (!strcmp(argv[i], "--no-fps")) settings.show_fps = 0;
    else if (!strcmp(argv[i], "--no-gl")) no_gl = 1;
    else if (!strcmp(argv[i], "--window") && i + 2 < argc) {
      win_w = atoi(argv[++i]);
      win_h = atoi(argv[++i]);
    }
    else if (!strcmp(argv[i], "--render-height") && i + 1 < argc) {
      int h = atoi(argv[++i]);
      settings.render_index = 0;
      for (int k = 0; k < render_height_count; k++)
        if (render_heights[k] == h) settings.render_index = k;
    }
    else if (!strcmp(argv[i], "--scale-mode") && i + 1 < argc) {
      const char *m = argv[++i];
      settings.video.scale_mode = !strcmp(m, "integer") ? SCALE_INTEGER : !strcmp(m, "stretch") ? SCALE_STRETCH : SCALE_FIT;
    }
    else if (!strcmp(argv[i], "--square-pixels")) settings.video.aspect_43 = 0;
    else if (!strcmp(argv[i], "--format") && i + 1 < argc) {
      const char *f = argv[++i];
      settings.video.screen_format = !strcmp(f, "16:9") ? FORMAT_16_9 : !strcmp(f, "21:9") ? FORMAT_21_9 : FORMAT_4_3;
    }
    else if (!strcmp(argv[i], "--crt") && i + 1 < argc) {
      const char *m = argv[++i];
      settings.video.crt = !strcmp(m, "scanlines") ? CRT_SCANLINES : !strcmp(m, "aperture") ? CRT_APERTURE_GRILLE :
                  !strcmp(m, "slot") ? CRT_SLOT_MASK : !strcmp(m, "shadow") ? CRT_SHADOW_MASK : CRT_OFF;
    }
    else if (!strcmp(argv[i], "--curvature") && i + 1 < argc) settings.video.curvature = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--screenshot") && i + 2 < argc) {
      char *end;
      shot_frame = shot_last = strtol(argv[++i], &end, 10);
      if (*end == '-')
        shot_last = strtol(end + 1, NULL, 10);
      shot_file = argv[++i];
    }
    else if (!strcmp(argv[i], "--ram-log") && i + 2 < argc) {
      ram_log = fopen(argv[++i], "w");
      ram_log_every = atol(argv[++i]);
      if (ram_log_every < 1) ram_log_every = 1;
    }
    else if (!strcmp(argv[i], "--no-rom-check")) rom_check = 0;
    else if (!strcmp(argv[i], "--fps") && i + 1 < argc) cli_fps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--data") && i + 1 < argc) i++;
    else if (!strcmp(argv[i], "--input") && i + 1 < argc) load_script(argv[++i]);
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frame_limit = atol(argv[++i]);
    else usage();
  }
  char rom_default[1100];
  if (!rom) {
    char dir[1024];
    game_directory(dir, sizeof dir);
    snprintf(rom_default, sizeof rom_default, "%sbaserom.md", dir);
    rom = rom_default;
    FILE *probe = fopen(rom, "rb");
    if (probe)
      fclose(probe);
    else if ((probe = fopen("rom/baserom.md", "rb"))) {
      fclose(probe);
      rom = "rom/baserom.md";
    }
  }
  if (md_load_rom(rom))
    return fatal("Cannot read the ROM %s\n\nCopy your Super Hang-On (Japan, USA) ROM next to "
                 "the game (file name baserom.md) or pass --rom PATH.", rom);
  char digest[41];
  sha1(md.rom, md.rom_size, digest);
  if (strcmp(digest, ROM_SHA1)) {
    if (rom_check)
      return fatal("%s is not the expected ROM\n(SHA1 %s, expected %s)\n\n"
                   "The game code was built from Super Hang-On (Japan, USA) (En,Ja).", rom, digest, ROM_SHA1);
    fprintf(stderr, "shangon: %s is not the expected ROM (SHA1 %s), continuing\n", rom, digest);
  }
  /* code added by the source overlays (patches/pc60) */
  /* frame rate: 60 or 120 logic ticks and video frames per second */
  int want_rate = (cli_fps ? cli_fps : settings.frame_rate) == 120 ? 2 : 1;
#if defined(RT_CODE_SETS_RATES)
  /* the rate can also be changed while playing (rate_apply): the ROM without
   * the overlays of either rate is kept to build the other code from */
  snprintf(data_path, sizeof data_path, "%s", data);
  rom_clean = malloc(md.rom_size);
  if (!rom_clean)
    return fatal("Out of memory.");
  memcpy(rom_clean, md.rom, md.rom_size);
  menu_rate_choice = 1;
  if (rate_apply(want_rate) != 0)
    return fatal("%s: the game code could not be decoded.", rom);
  settings.frame_rate = 60 * rt_rate;
#elif defined(RT_CODE_SET_ORIGINAL)
  const RtCodeSet *code_set = &rt_code_set_original;
  (void)want_rate;
#else
  rt_rate = SHANGON_FIXED_RATE;
  (void)want_rate;
#endif
#ifndef RT_CODE_SETS_RATES
  menu_rate = rt_rate;
  FPS = 60 * rt_rate;
#ifdef RT_TRANSLATE
  const RomPatch *rom_patches = code_set->patches;
  int rom_patch_count = *code_set->patch_count;
#endif
  for (int i = 0; i < rom_patch_count; i++)
    if (rom_patches[i].addr + rom_patches[i].len <= md.rom_size)
      memcpy(md.rom + rom_patches[i].addr, rom_patches[i].bytes, rom_patches[i].len);
#ifdef RT_TRANSLATE
  /* the game code: decoded from the ROM now, or read from the cache made at
   * the first start */
  char cache[1100];
  snprintf(cache, sizeof cache, rt_rate == 2 ? "%sshangon120.cache" : "%sshangon.cache", data);
  if (rt_translate_init(code_set, md.rom, md.rom_size, cache) != 0)
    return fatal("%s: the game code could not be decoded.", rom);
#endif
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    return fatal("SDL_Init: %s", SDL_GetError());
  }
  atexit(SDL_Quit);
  if (fullscreen)
    settings.fullscreen = 1;
  if (scale > 0 && (win_w <= 0 || win_h <= 0)) {    /* --scale N: N times the native height */
    win_h = MD_MAX_H * scale;
    win_w = settings.video.aspect_43 ? win_h * 4 / 3 : 320 * scale;
  }
  if (win_w <= 0 || win_h <= 0) {
    win_w = window_presets[settings.window_preset].w;
    win_h = window_presets[settings.window_preset].h;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
      if (win_w > usable.w) { win_h = win_h * usable.w / win_w; win_w = usable.w; }
      if (win_h > usable.h) { win_w = win_w * usable.h / win_h; win_h = usable.h; }
    }
  }
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (!no_gl) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Super Hang-On", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              win_w, win_h, flags | SDL_WINDOW_OPENGL);
    if (!window)
      fprintf(stderr, "shangon: no OpenGL window (%s)\n", SDL_GetError());
  }
  {
    SDL_DisplayMode mode;
    refresh_hz = SDL_GetCurrentDisplayMode(0, &mode) == 0 ? mode.refresh_rate : 0;
  }
  apply_vsync();
  if (window && video_init(window, use_vsync) == 0) {
    use_gl = 1;
  } else {
    if (window) {
      fprintf(stderr, "shangon: OpenGL unavailable, using the basic renderer (no CRT filters)\n");
      video_shutdown();
      SDL_DestroyWindow(window);
    }
    window = SDL_CreateWindow("Super Hang-On", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
    if (!window)
      return fatal("SDL_CreateWindow: %s", SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (use_vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!renderer)
      return fatal("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
  }
  perf_freq = SDL_GetPerformanceFrequency();

  char controls[1100];
  snprintf(controls, sizeof controls, "%scontrols.ini", data);
  input_init(controls);
  if (!mute)
    open_audio();
  md_init();
  persist_init(data);
  rt_frame_end_callback = on_frame_end;
  start_time = SDL_GetPerformanceCounter();
  rt_frame_callback = on_frame;
  M68K c;
  rt_start(&c);
  return 0;
}
