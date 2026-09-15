#include "state.h"
#include "recomp_rt.h"

#define STATE_MAGIC 0x53484f53u               /* "SHOS" */
#define STATE_VERSION 1

uint32_t state_build_id(void)
{
  /* FNV-1a over the recompiled block addresses and the state format */
  uint32_t h = 2166136261u;
  for (int i = 0; i < rt_block_count; i++) {
    uint32_t a = rt_blocks[i].addr;
    for (int k = 0; k < 4; k++) {
      h ^= (a >> (k * 8)) & 0xff;
      h *= 16777619u;
    }
  }
  return h ^ STATE_VERSION;
}

static void machine(StateIO *io, M68K *c)
{
  uint32_t magic = STATE_MAGIC, build = state_build_id();
  STATE_VAR(io, magic);
  STATE_VAR(io, build);
  if (io->loading && (magic != STATE_MAGIC || build != state_build_id())) {
    io->error = 1;
    return;
  }
  rt_state(io, c);
  md_state(io);
  vdp_state(io);
  render_state(io);
  audio_state(io);
}

size_t state_max_size(void)
{
  static size_t n;
  if (!n) {
    /* all sections have a fixed size: measure with a save into scratch space */
    static uint8_t scratch[1 << 20];
    static M68K dummy;
    StateIO io = {scratch, sizeof scratch, 0, 0, 0};
    machine(&io, &dummy);
    n = io.pos;
  }
  return n;
}

size_t state_save(M68K *c, uint8_t *buf, size_t size)
{
  StateIO io = {buf, size, 0, 0, 0};
  machine(&io, c);
  return io.error ? 0 : io.pos;
}

int state_load(M68K *c, const uint8_t *buf, size_t size)
{
  /* validate the header before touching the machine */
  StateIO probe = {(uint8_t *)buf, size, 0, 1, 0};
  uint32_t magic = 0, build = 0;
  STATE_VAR(&probe, magic);
  STATE_VAR(&probe, build);
  if (probe.error || magic != STATE_MAGIC || build != state_build_id() || size != state_max_size())
    return -1;
  StateIO io = {(uint8_t *)buf, size, 0, 1, 0};
  machine(&io, c);
  return io.error ? -1 : 0;
}
