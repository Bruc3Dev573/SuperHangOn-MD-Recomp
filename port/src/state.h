/*
 * Save states: every module serialises its state through one symmetric
 * function (the same code saves and loads).
 */
#ifndef STATE_H
#define STATE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "m68k_rt.h"

typedef struct {
  uint8_t *buf;
  size_t size, pos;
  int loading;
  int error;
} StateIO;

static inline void state_bytes(StateIO *io, void *p, size_t n)
{
  if (io->pos + n > io->size) {
    io->error = 1;
    return;
  }
  if (io->loading)
    memcpy(p, io->buf + io->pos, n);
  else
    memcpy(io->buf + io->pos, p, n);
  io->pos += n;
}

#define STATE_VAR(io, v) state_bytes((io), &(v), sizeof(v))

void rt_state(StateIO *io, M68K *c);
void md_state(StateIO *io);
void vdp_state(StateIO *io);
void render_state(StateIO *io);
void audio_state(StateIO *io);

/* identifies the build: states of other builds are refused */
uint32_t state_build_id(void);

/* whole machine; returns the state size, or 0 on error (load: bad state) */
size_t state_save(M68K *c, uint8_t *buf, size_t size);
int state_load(M68K *c, const uint8_t *buf, size_t size);
size_t state_max_size(void);

#endif
