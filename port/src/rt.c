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
#include <setjmp.h>
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
int rt_rate = 1;
/* RAM byte the 120 Hz race tick wait sets while it waits (patches/pc60) */
#define FAST_VBLANK_FLAG 0xffc626
static uint32_t frame_no;
static int32_t after_vblank = 1;      /* the previous frame ended with the game's VBlank */
void (*rt_frame_callback)(M68K *c);
void (*rt_frame_end_callback)(M68K *c, uint32_t resume_pc);

static jmp_buf top_loop;
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
  /* at 120 frames per second a frame is half the hardware time: the sound
   * chips run half as long and their interrupt comes every second frame; the
   * game's VBlank comes every frame only while the race waits for its next
   * tick, else every second frame (menus keep their 60 Hz timing) */
  uint32_t line_mcycles = MD_MCYCLES_PER_LINE / (uint32_t)rt_rate;
  int tick60 = frame_no++ % (uint32_t)rt_rate == 0;
  int vblank = tick60 || m68k_read8(FAST_VBLANK_FLAG);
  /* active display: after a frame without the game's VBlank (120 frames per
   * second outside the race ticks) the picture of the previous frame is shown
   * again, as its line scroll comes from tables the VBlank resets */
  vdp.status &= ~0x0008;
  if (!after_vblank) {
    for (int line = 0; line < 224; line++) {
      vdp.line = line;
      audio_run(line_mcycles);
    }
    goto active_done;
  }
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
    audio_run(line_mcycles);
  }
active_done:
  if (rt_frame_callback)
    rt_frame_callback(c);

  /* vertical blank */
  vdp.line = 224;
  vdp.status |= vblank ? 0x0088 : 0x0008;
  after_vblank = vblank;
  audio_set_z80_int(tick60);
  if (vblank && (vdp.reg[1] & 0x20)) {
    if (c->imask < 6) {
      vint_pending = 0;
      vdp.status &= ~0x0080;
      run_interrupt(c, 6, resume_pc);
    } else {
      vint_pending = 1;
    }
  }
  audio_run(line_mcycles);                        /* the Z80 interrupt lasts one line */
  audio_set_z80_int(0);
  for (unsigned line = 225; line < MD_LINES_PER_FRAME; line++) {
    vdp.line = (int)line;
    audio_run(line_mcycles);
  }
  vdp.line = 0;
  rt_cycles = 0;
  c->pc = resume_pc;                              /* saved with the state: where to resume */
  if (rt_frame_end_callback)
    rt_frame_end_callback(c, resume_pc);
}

void rt_resume_at(M68K *c, uint32_t pc)
{
  (void)c;
  jump_pc = pc & 0xffffff;
  longjmp(top_loop, 1);
}

void rt_state(StateIO *io, M68K *c)
{
  STATE_VAR(io, *c);
  STATE_VAR(io, vint_pending);
  STATE_VAR(io, rt_cycles);
  STATE_VAR(io, frame_no);
  STATE_VAR(io, after_vblank);
}

void rt_wait_point(M68K *c, uint32_t addr)
{
  frame(c, addr);
}

void rt_start(M68K *c)
{
#ifndef RT_TRANSLATE
  build_table();
#endif
  memset(c, 0, sizeof *c);
  c->s = 1;
  c->imask = 7;
  c->a[7] = m68k_read32(0);
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
    if (rt_cycles > HANG_GUARD_CYCLES / (uint32_t)rt_rate)
      frame(c, pc);
  }
}
