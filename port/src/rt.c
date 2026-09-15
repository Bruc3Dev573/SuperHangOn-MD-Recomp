/*
 * Execution runtime for the recompiled game: block dispatch, interrupts and
 * the per-frame hardware sequence.
 *
 * Game code runs until it busy-waits (rt_wait_point) or has used a lot of
 * CPU time without waiting (hang guard); then one video frame of hardware is
 * produced: active lines with the HBlank effect, the host callback (render,
 * audio, input, pacing) and the VBlank interrupt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <setjmp.h>
#else
#include <emscripten.h>
#endif
#include "recomp_rt.h"
#include "md.h"
#include "audio.h"
#include "state.h"
#ifdef RT_TRANSLATE
#include "rt_translate.h"
#endif

#define CODE_LIMIT 0x80000
#define HANG_GUARD_CYCLES (128000u * 8)     /* 8 frames of 68000 time without waiting */

uint32_t rt_cycles;
void (*rt_frame_callback)(M68K *c);
void (*rt_frame_end_callback)(M68K *c, uint32_t resume_pc);

#ifndef __EMSCRIPTEN__
static jmp_buf top_loop;
#else
void (*rt_render_callback)(void);
int (*rt_menu_tick_callback)(void);
static M68K *loop_cpu;
static uint32_t loop_pc;
static int frame_done;
static int resume_pending;
static int rt_target_fps = 60;
static double rt_next_frame_ms;
#endif
static uint32_t jump_pc;

static int vint_pending;

#ifndef RT_TRANSLATE
static RtBlockFn block_table[CODE_LIMIT / 2];

static void build_table(void)
{
  for (int i = 0; i < rt_block_count; i++)
    if (rt_blocks[i].addr < CODE_LIMIT)
      block_table[rt_blocks[i].addr >> 1] = rt_blocks[i].fn;
}
#endif

uint32_t rt_bad_pc(M68K *c, uint32_t pc)
{
  fprintf(stderr, "recomp: no block at $%06x (sr=%04x sp=%08x)\n", pc, get_sr(c), c->a[7]);
  exit(3);
}

uint32_t rt_unsupported(M68K *c, uint32_t pc)
{
  fprintf(stderr, "recomp: unsupported instruction at $%06x\n", pc);
  (void)c;
  exit(3);
}

void m68k_reset_devices(void) {}

/* runs the block at pc (recompiled, or translated at start-up); returns the
 * next address */
#ifdef RT_TRANSLATE
static inline uint32_t run_block(M68K *c, uint32_t pc)
{
  return rt_translate_run(c, pc);
}
#else
static inline uint32_t run_block(M68K *c, uint32_t pc)
{
  pc &= 0xffffff;
  if (pc >= CODE_LIMIT || (pc & 1) || !block_table[pc >> 1])
    rt_bad_pc(c, pc);
  return block_table[pc >> 1](c);
}
#endif

/* Run an interrupt handler to completion: exception entry, then blocks until
 * the handler returns to `return_pc` with the stack restored. */
static void run_interrupt(M68K *c, int level, uint32_t return_pc)
{
  uint32_t sp_before = c->a[7];
  uint8_t s_before = c->s;
  uint32_t pc = m68k_exception(c, 24 + level, return_pc);
  c->imask = level;
  do {
    pc = run_block(c, pc) & 0xffffff;
  } while (!(pc == (return_pc & 0xffffff) && c->s == s_before && c->a[7] == sp_before));
}

/* The HBlank handler the game copies to RAM ($FF05D2) writes one VSRAM entry
 * per line from a pointer kept in its own operand; it is emulated here. */
static void hblank_effect(M68K *c, int line)
{
  (void)c;
  if (line == 0xdf)
    vdp_write_ctrl(0x8004);                       /* disable line interrupts */
  vdp_write_ctrl(0x4000);
  vdp_write_ctrl(0x0010);                         /* VSRAM address 0 */
  uint32_t ptr = m68k_read32(0xff05f0);
  uint32_t v = m68k_read32(ptr);
  vdp_write_data(v >> 16);
  vdp_write_data(v & 0xffff);
  m68k_write16(0xff05f2, (m68k_read16(0xff05f2) + 4) & 0xffff);
}

static int hblank_installed(void)
{
  uint32_t vec = m68k_read32(0x70) & 0xffffff;
  return vec == 0xff05d2 && m68k_read16(0xff05fe) == 0x4e73;   /* ends with rte */
}

static void frame(M68K *c, uint32_t resume_pc)
{
  /* active display */
  vdp.status &= ~0x0008;
  int counter = vdp.reg[10];
  /* the line interrupt counter also runs on the pre-render line, so the first
   * HBlank effect happens before line 0 (matches Genesis Plus GX) */
  if (--counter < 0) {
    counter = vdp.reg[10];
    if ((vdp.reg[0] & 0x10) && c->imask < 4 && hblank_installed())
      hblank_effect(c, -1);
  }
  for (int line = 0; line < 224; line++) {
    vdp.line = line;
    vdp.line_vscroll[line][0] = vdp.vsram[0];
    vdp.line_vscroll[line][1] = vdp.vsram[1];
    if (--counter < 0) {
      counter = vdp.reg[10];
      if ((vdp.reg[0] & 0x10) && c->imask < 4 && hblank_installed())
        hblank_effect(c, line);
    }
    audio_run(MD_MCYCLES_PER_LINE);
  }
  if (rt_frame_callback)
    rt_frame_callback(c);

  /* vertical blank */
  vdp.line = 224;
  vdp.status |= 0x0088;
  audio_set_z80_int(1);
  if (vdp.reg[1] & 0x20) {
    if (c->imask < 6) {
      vint_pending = 0;
      vdp.status &= ~0x0080;
      run_interrupt(c, 6, resume_pc);
    } else {
      vint_pending = 1;
    }
  }
  audio_run(MD_MCYCLES_PER_LINE);                 /* the Z80 interrupt lasts one line */
  audio_set_z80_int(0);
  for (unsigned line = 225; line < MD_LINES_PER_FRAME; line++) {
    vdp.line = (int)line;
    audio_run(MD_MCYCLES_PER_LINE);
  }
  vdp.line = 0;
  rt_cycles = 0;
  c->pc = resume_pc;                              /* saved with the state: where to resume */
  if (rt_frame_end_callback)
    rt_frame_end_callback(c, resume_pc);
#ifdef __EMSCRIPTEN__
  frame_done = 1;
#endif
}

void rt_resume_at(M68K *c, uint32_t pc)
{
  (void)c;
  jump_pc = pc & 0xffffff;
#ifdef __EMSCRIPTEN__
  resume_pending = 1;
#else
  longjmp(top_loop, 1);
#endif
}

void rt_state(StateIO *io, M68K *c)
{
  STATE_VAR(io, *c);
  STATE_VAR(io, vint_pending);
  STATE_VAR(io, rt_cycles);
}

void rt_wait_point(M68K *c, uint32_t addr)
{
  frame(c, addr);
}

void rt_set_fps(int fps)
{
#ifdef __EMSCRIPTEN__
  rt_target_fps = fps == 120 ? 120 : 60;
  rt_next_frame_ms = 0;
#else
  (void)fps;
#endif
}

#ifdef __EMSCRIPTEN__
static void run_frame(M68K *c)
{
  frame_done = 0;
  for (;;) {
    loop_pc = run_block(c, loop_pc) & 0xffffff;
    if (resume_pending) {
      loop_pc = jump_pc;
      resume_pending = 0;
    }
    if (vint_pending && c->imask < 6) {
      vint_pending = 0;
      run_interrupt(c, 6, loop_pc);
    }
    if (frame_done)
      return;
    if (rt_cycles > HANG_GUARD_CYCLES)
      frame(c, loop_pc);
  }
}

static void rt_tick(void *arg)
{
  M68K *c = arg;
  if (rt_menu_tick_callback && rt_menu_tick_callback())
    return;
  double now = emscripten_get_now();
  double period = 1000.0 / rt_target_fps;
  if (!rt_next_frame_ms)
    rt_next_frame_ms = now;
  int frames = 0;
  while (now + 0.25 >= rt_next_frame_ms && frames < 4) {
    rt_next_frame_ms += period;
    run_frame(c);
    frames++;
  }
  if (now > rt_next_frame_ms + period * 8)
    rt_next_frame_ms = now + period;
  if (!frames && rt_render_callback)
    rt_render_callback();
}
#endif

void rt_start(M68K *c)
{
#ifndef RT_TRANSLATE
  build_table();
#endif
  memset(c, 0, sizeof *c);
  c->s = 1;
  c->imask = 7;
  c->a[7] = m68k_read32(0);
#ifdef __EMSCRIPTEN__
  loop_cpu = c;
  loop_pc = m68k_read32(4) & 0xffffff;
  rt_next_frame_ms = 0;
  emscripten_set_main_loop_arg(rt_tick, loop_cpu, 0, 1);
#else
  static uint32_t pc;
  pc = m68k_read32(4) & 0xffffff;
  if (setjmp(top_loop))
    pc = jump_pc;                     /* a state was loaded: continue at its resume point */
  for (;;) {
    pc = run_block(c, pc) & 0xffffff;
    if (vint_pending && c->imask < 6) {
      vint_pending = 0;
      run_interrupt(c, 6, pc);
    }
    if (rt_cycles > HANG_GUARD_CYCLES)
      frame(c, pc);
  }
#endif
}
