/*
 * Runtime interface used by the recompiled game code (port/gen/).
 */
#ifndef RECOMP_RT_H
#define RECOMP_RT_H

#include <stdint.h>
#include "m68k_rt.h"

typedef uint32_t (*RtBlockFn)(M68K *c);
typedef struct {
  uint32_t addr;
  RtBlockFn fn;
} RtBlock;

extern const RtBlock rt_blocks[];
extern const int rt_block_count;

/* video frames per 1/60 s (1 or 2): must match the TickShift of the game code */
extern int rt_rate;

/* changes the rate while the game runs (the game code of the new rate must be
 * in place): the next frame is the first of a 1/60 s period again */
void rt_set_rate(int rate);

/* approximate 68000 cycles executed since the last video frame */
extern uint32_t rt_cycles;
static inline void rt_charge(uint32_t cycles) { rt_cycles += cycles; }

/* the game is busy-waiting (loop at `addr` repeats): let a video frame pass */
void rt_wait_point(M68K *c, uint32_t addr);

uint32_t rt_bad_pc(M68K *c, uint32_t pc);
uint32_t rt_unsupported(M68K *c, uint32_t pc);
void m68k_reset_devices(void);

#ifdef RT_HOOKS
void rt_hook(M68K *c, uint32_t pc);
#define RT_HOOK(c, pc) rt_hook((c), (pc))
#else
#define RT_HOOK(c, pc) ((void)0)
#endif

/* ---- host side ---------------------------------------------------------- */

/* runs the game from the reset vector; never returns */
void rt_start(M68K *c);
/* one video frame of hardware: HBlank effects, VBlank interrupt, then the
 * host callback (render / audio / input / pacing) */
extern void (*rt_frame_callback)(M68K *c);
#ifdef __EMSCRIPTEN__
extern void (*rt_render_callback)(void);
extern int (*rt_menu_tick_callback)(void);
#endif
/* end of a video frame, before the game resumes at `resume_pc`: the machine
 * state is consistent here (save states) */
extern void (*rt_frame_end_callback)(M68K *c, uint32_t resume_pc);
/* selects the game tick rate; native builds pace locally, browser builds
 * schedule the emulation against the display callback */
void rt_set_fps(int fps);
/* abandon the current execution (after loading a state) and continue at pc;
 * only valid from rt_frame_end_callback */
void rt_resume_at(M68K *c, uint32_t pc);

#endif
