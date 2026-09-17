#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "state.h"
#include "recomp_rt.h"
#include "md.h"
#include "scene.h"

#define REWIND_FRAMES (120 * 20)          /* ring capacity: 20 s at 120 frames per second */
#define REWIND_BYTES ((size_t)96 << 20)

static char dir[1024];
static int slot;
static char message[64];

static size_t state_size;
static uint8_t *cur, *next;          /* state at the end of the last frame / this frame */
static int have_cur;
static uint8_t *rle_scratch;

/* rewind ring: deltas (current XOR previous state), RLE compressed */
static struct { uint8_t *data; size_t len; } ring[REWIND_FRAMES];
static int ring_head, ring_count;
static size_t ring_bytes;

/* ---- records ------------------------------------------------------------ */

/* ranking tables (4 courses x 7 entries) and the records set by InitRecords */
static const struct { uint32_t addr, len; } record_areas[] = {
  {0xff0200, 0x1c0},
  {0xff0478, 0x70},
};
#define N_AREAS (sizeof record_areas / sizeof record_areas[0])
#define RECORDS_SIZE (0x1c0 + 0x70)
static uint8_t records[RECORDS_SIZE];
static int records_applied;
static unsigned records_timer;
static const char records_magic[8] = "SHREC01";

static int game_initialised(void)
{
  return !memcmp(md.ram, "init", 4);
}

static void records_copy(int to_ram)
{
  size_t off = 0;
  for (size_t i = 0; i < N_AREAS; i++) {
    uint8_t *ram = md.ram + (record_areas[i].addr & 0xffff);
    if (to_ram) memcpy(ram, records + off, record_areas[i].len);
    else memcpy(records + off, ram, record_areas[i].len);
    off += record_areas[i].len;
  }
}

static int records_differ(void)
{
  size_t off = 0;
  for (size_t i = 0; i < N_AREAS; i++) {
    if (memcmp(md.ram + (record_areas[i].addr & 0xffff), records + off, record_areas[i].len))
      return 1;
    off += record_areas[i].len;
  }
  return 0;
}

static void records_path(char *path, size_t n)
{
  snprintf(path, n, "%srecords.bin", dir);
}

static void records_write(void)
{
  char path[1100], tmp[1110];
  records_path(path, sizeof path);
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return;
  int ok = fwrite(records_magic, 1, 8, f) == 8 && fwrite(records, 1, RECORDS_SIZE, f) == RECORDS_SIZE;
  ok &= fclose(f) == 0;
  if (ok) {
    remove(path);                    /* rename does not replace on Windows */
    rename(tmp, path);
  }
}

static void records_frame(void)
{
  if (!game_initialised()) {
    records_applied = 0;
    return;
  }
  if (!records_applied) {
    /* first frame after the cold-boot initialisation: bring back saved records */
    char path[1100];
    records_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    uint8_t buf[8 + RECORDS_SIZE];
    int loaded = 0;
    if (f) {
      if (fread(buf, 1, sizeof buf, f) == sizeof buf && !memcmp(buf, records_magic, 8)) {
        memcpy(records, buf + 8, RECORDS_SIZE);
        records_copy(1);
        loaded = 1;
      }
      fclose(f);
    }
    records_copy(0);
    if (!loaded)
      records_write();
    records_applied = 1;
    return;
  }
  if (++records_timer % 60 == 0 && records_differ()) {
    records_copy(0);
    records_write();
  }
}

/* ---- states ------------------------------------------------------------- */

static void slot_path(char *path, size_t n, int s)
{
  snprintf(path, n, "%sstate%d.sst", dir, s);
}

static size_t rle_encode(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out)
{
  size_t i = 0, o = 0;
  while (i < n) {
    size_t z = i;
    while (z < n && a[z] == b[z]) z++;
    size_t l = z;
    while (l < n && (a[l] != b[l] || (l + 1 < n && a[l + 1] != b[l + 1]))) l++;
    size_t zeros = z - i, lits = l - z;
    for (size_t v = zeros; ; v >>= 7) { out[o++] = (uint8_t)((v & 0x7f) | (v > 0x7f ? 0x80 : 0)); if (v <= 0x7f) break; }
    for (size_t v = lits; ; v >>= 7) { out[o++] = (uint8_t)((v & 0x7f) | (v > 0x7f ? 0x80 : 0)); if (v <= 0x7f) break; }
    for (size_t k = z; k < l; k++)
      out[o++] = a[k] ^ b[k];
    i = l;
  }
  return o;
}

/* applies a delta to `state` in place */
static void rle_apply(uint8_t *state, size_t n, const uint8_t *in, size_t len)
{
  size_t i = 0, p = 0;
  while (p < len && i < n) {
    size_t zeros = 0, lits = 0;
    int shift = 0;
    do { zeros |= (size_t)(in[p] & 0x7f) << shift; shift += 7; } while (in[p++] & 0x80);
    shift = 0;
    do { lits |= (size_t)(in[p] & 0x7f) << shift; shift += 7; } while (in[p++] & 0x80);
    i += zeros;
    for (size_t k = 0; k < lits && i < n; k++)
      state[i++] ^= in[p++];
  }
}

static void ring_drop_oldest(void)
{
  int idx = (ring_head - ring_count + REWIND_FRAMES) % REWIND_FRAMES;
  ring_bytes -= ring[idx].len;
  free(ring[idx].data);
  ring[idx].data = NULL;
  ring_count--;
}

static void ring_clear(void)
{
  while (ring_count)
    ring_drop_oldest();
}

void persist_rate_changed(void)
{
  /* the recorded frames last half or twice as long as the ones that follow:
   * rewinding through them would run at the wrong speed */
  ring_clear();
}

static uint8_t *boot;                 /* machine state at the end of the first frame */
static int reset_requested;

void persist_request_reset(void)
{
  reset_requested = 1;
}

void persist_init(const char *d)
{
  snprintf(dir, sizeof dir, "%s", d ? d : "");
  state_size = state_max_size();
  boot = malloc(state_size);
  cur = malloc(state_size);
  next = malloc(state_size);
  rle_scratch = malloc(state_size * 2 + 16);
}

static void after_load(M68K *c)
{
  /* the wide picture is built from a snapshot of the RAM taken at the end of
   * the frame: the loaded state replaces it, so that it matches the VDP state
   * the next frame is displayed with (a rewound frame stayed 4:3 without it) */
  scene_frame_end();
  /* records live outside the states: never let a state take them back */
  if (records_applied && game_initialised())
    records_copy(1);
  memcpy(cur, next, state_size);
  have_cur = 1;
  rt_resume_at(c, c->pc);
}

void persist_frame_end(M68K *c, int save, int load, int rewind)
{
  static int have_boot;
  /* the state the game is reset to: taken once the game has initialised, not
   * during the boot (it sums the whole ROM there, and a reset after a change
   * of frame rate would finish that sum over the other build of the code) */
  if (!have_boot && boot && game_initialised())
    have_boot = state_save(c, boot, state_size) != 0;
  if (reset_requested) {
    reset_requested = 0;
    if (records_applied && game_initialised() && records_differ()) {
      records_copy(0);                              /* a record of the last second */
      records_write();
    }
    if (have_boot && state_load(c, boot, state_size) == 0) {
      snprintf(message, sizeof message, "Game reset");
      ring_clear();
      records_applied = 0;                          /* loaded again once the game has initialised */
      memcpy(cur, boot, state_size);
      have_cur = 1;
      rt_resume_at(c, c->pc);                       /* does not return */
    }
  }

  records_frame();

  if (rewind) {
    if (have_cur && ring_count) {
      ring_head = (ring_head - 1 + REWIND_FRAMES) % REWIND_FRAMES;
      ring_count--;
      memcpy(next, cur, state_size);
      rle_apply(next, state_size, ring[ring_head].data, ring[ring_head].len);
      ring_bytes -= ring[ring_head].len;
      free(ring[ring_head].data);
      ring[ring_head].data = NULL;
      if (state_load(c, next, state_size) == 0)
        after_load(c);                                /* does not return */
    }
    return;
  }

  if (save) {
    char path[1100];
    slot_path(path, sizeof path, slot);
    FILE *f = fopen(path, "wb");
    size_t n = state_save(c, next, state_size);
    int ok = f && n && fwrite(next, 1, n, f) == n;
    if (f) ok &= fclose(f) == 0;
    snprintf(message, sizeof message, ok ? "State %d saved" : "State %d: cannot save", slot);
  }

  if (load) {
    char path[1100];
    slot_path(path, sizeof path, slot);
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(next, 1, state_size, f) : 0;
    if (f) fclose(f);
    if (!f) {
      snprintf(message, sizeof message, "State %d is empty", slot);
    } else if (n != state_size || state_load(c, next, state_size)) {
      snprintf(message, sizeof message, "State %d is from another version", slot);
    } else {
      snprintf(message, sizeof message, "State %d loaded", slot);
      ring_clear();
      after_load(c);                                  /* does not return */
    }
  }

  /* record this frame for rewinding */
  if (!state_save(c, next, state_size))
    return;
  if (have_cur) {
    size_t len = rle_encode(next, cur, state_size, rle_scratch);
    uint8_t *data = malloc(len);
    if (data) {
      memcpy(data, rle_scratch, len);
      if (ring_count >= 60 * 20 * rt_rate)             /* 20 seconds */
        ring_drop_oldest();
      ring[ring_head].data = data;
      ring[ring_head].len = len;
      ring_head = (ring_head + 1) % REWIND_FRAMES;
      ring_count++;
      ring_bytes += len;
      while (ring_bytes > REWIND_BYTES && ring_count > 1)
        ring_drop_oldest();
    }
  }
  uint8_t *t = cur;
  cur = next;
  next = t;
  have_cur = 1;
}

int persist_slot(void)
{
  return slot;
}

void persist_set_slot(int s)
{
  slot = (s % 10 + 10) % 10;
  snprintf(message, sizeof message, "Slot %d", slot);
}

const char *persist_message(void)
{
  static char out[64];
  if (!message[0])
    return NULL;
  memcpy(out, message, sizeof out);
  message[0] = 0;
  return out;
}
