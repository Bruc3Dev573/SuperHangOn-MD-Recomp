/*
 * Sound hardware of the Mega Drive as used by the game.
 *
 * Z80 memory map: $0000-$1FFF RAM (mirrored to $3FFF), $4000-$5FFF YM2612,
 * $6000-$60FF bank register (9 serial writes of bit 0), $7F11-$7F17 PSG,
 * $8000-$FFFF window on the 68000 space selected by the bank register.
 *
 * Timing in master-clock cycles (NTSC 53.693175 MHz): Z80 = 15, YM2612
 * internal clock = 42 (24 clocks per output sample), PSG clock = 240. The
 * YM2612 and PSG are run up to the Z80's time before every access, so writes
 * land on the right sample.
 */
#include <string.h>
#include "audio.h"
#include "md.h"
#include "m68k_rt.h"
#include "state.h"

#define CHIPS_IMPL
#include "z80.h"
#include "ym3438.h"

#define Z80_MCYCLES 15
#define YM_MCYCLES 42
#define PSG_MCYCLES 240
#define OUT_CAPACITY 32768                    /* stereo frames */

void (*audio_ym_log)(unsigned port, unsigned value);
void (*audio_psg_log)(unsigned value);
static int64_t audio_time, z80_time, ym_time, psg_time;

static z80_t z80;
static uint64_t z80_pins;
static int z80_int;
static uint32_t zbank;                        /* 68000 address of the $8000 window */
static ym3438_t ym;

static int ym_clock;
static int32_t fm_acc[2];

static int16_t out_buf[OUT_CAPACITY * 2];
static size_t out_head, out_count;

/* ---- PSG (SN76489 as integrated in the VDP) --------------------------------- */

/* maximum channel level and the FM/PSG balance of the reference (Genesis Plus
 * GX defaults: FM samples x11, PSG channels up to 2800 x 1.5) */
#define PSG_MAX 4200
static const uint16_t psg_volume[16] = {
  PSG_MAX, PSG_MAX * 794 / 1000, PSG_MAX * 631 / 1000, PSG_MAX * 501 / 1000,
  PSG_MAX * 398 / 1000, PSG_MAX * 316 / 1000, PSG_MAX * 251 / 1000, PSG_MAX * 200 / 1000,
  PSG_MAX * 158 / 1000, PSG_MAX * 126 / 1000, PSG_MAX * 100 / 1000, PSG_MAX * 79 / 1000,
  PSG_MAX * 63 / 1000, PSG_MAX * 50 / 1000, PSG_MAX * 40 / 1000, 0
};

static struct {
  uint16_t tone[3];                 /* 10-bit dividers */
  uint8_t volume[4];
  uint8_t noise;                    /* bit 2 = white, bits 1-0 = rate */
  uint8_t latch;                    /* register selected by the last latch byte */
  uint16_t counter[4];
  uint8_t output[4];
  uint16_t lfsr;
  int32_t acc;
  int acc_count;
} psg;

static void psg_reset(void)
{
  memset(&psg, 0, sizeof psg);
  for (int i = 0; i < 4; i++)
    psg.volume[i] = 15;
  psg.lfsr = 0x8000;
}

static void md_psg_write_now(uint8_t v)
{
  if (v & 0x80)
    psg.latch = (v >> 4) & 7;
  unsigned ch = psg.latch >> 1;
  if (psg.latch & 1) {
    psg.volume[ch] = v & 0x0f;
  } else if (ch < 3) {
    if (v & 0x80)
      psg.tone[ch] = (psg.tone[ch] & 0x3f0) | (v & 0x0f);
    else
      psg.tone[ch] = (psg.tone[ch] & 0x00f) | ((v & 0x3f) << 4);
  } else {
    psg.noise = v & 7;
    psg.lfsr = 0x8000;
  }
}

static void psg_clock(void)
{
  for (int i = 0; i < 3; i++) {
    if (psg.counter[i] == 0 || --psg.counter[i] == 0) {
      psg.counter[i] = psg.tone[i] ? psg.tone[i] : 1;
      psg.output[i] ^= 1;
    }
  }
  if (psg.counter[3] == 0 || --psg.counter[3] == 0) {
    unsigned rate = psg.noise & 3;
    psg.counter[3] = rate == 3 ? (psg.tone[2] ? psg.tone[2] : 1) : (0x10u << rate);
    psg.output[3] ^= 1;
    if (psg.output[3]) {                                /* shift on the rising edge */
      unsigned fb = (psg.noise & 4) ? (((psg.lfsr >> 0) ^ (psg.lfsr >> 3)) & 1) : (psg.lfsr & 1);
      psg.lfsr = (uint16_t)((psg.lfsr >> 1) | (fb << 15));
    }
  }
  int32_t s = 0;
  for (int i = 0; i < 3; i++)
    if (psg.output[i]) s += psg_volume[psg.volume[i]];
  if (psg.lfsr & 1) s += psg_volume[psg.volume[3]];
  psg.acc += s;
  psg.acc_count++;
}

static void psg_run_to(int64_t t)
{
  while (psg_time + PSG_MCYCLES <= t) {
    psg_clock();
    psg_time += PSG_MCYCLES;
  }
}

/* ---- YM2612 -------------------------------------------------------------- */

static void emit_sample(void)
{
  int32_t p = psg.acc_count ? psg.acc / psg.acc_count : 0;
  psg.acc = 0;
  psg.acc_count = 0;
  for (int c = 0; c < 2; c++) {
    int32_t s = fm_acc[c] * 11 + p;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    out_buf[((out_head + out_count) % OUT_CAPACITY) * 2 + c] = (int16_t)s;
    fm_acc[c] = 0;
  }
  if (out_count < OUT_CAPACITY)
    out_count++;
  else
    out_head = (out_head + 1) % OUT_CAPACITY;             /* nobody reads: drop oldest */
}

static void ym_run_to(int64_t t)
{
  while (ym_time + YM_MCYCLES <= t) {
    Bit16s buf[2];
    OPN2_Clock(&ym, buf);
    fm_acc[0] += buf[0];
    fm_acc[1] += buf[1];
    ym_time += YM_MCYCLES;
    psg_run_to(ym_time);
    if (++ym_clock == 24) {
      ym_clock = 0;
      emit_sample();
    }
  }
}

void md_ym_write(uint32_t port, uint8_t value)
{
  ym_run_to(z80_time > audio_time ? z80_time : audio_time);
  if (audio_ym_log)
    audio_ym_log(port & 3, value);
  OPN2_Write(&ym, port & 3, value);
}

void md_psg_write(uint8_t value)
{
  ym_run_to(z80_time > audio_time ? z80_time : audio_time);
  if (audio_psg_log)
    audio_psg_log(value);
  md_psg_write_now(value);
}

/* ---- Z80 ----------------------------------------------------------------- */

static uint8_t z80_read(uint16_t a)
{
  switch (a >> 13) {
    case 0: case 1:
      return md.zram[a & 0x1fff];
    case 2:
      ym_run_to(z80_time);
      return OPN2_Read(&ym, a & 3);
    case 3:
      return 0xff;
    default: {
      uint32_t addr = zbank | (a & 0x7fff);
      if (addr < 0x400000 || addr >= 0xe00000)
        return (uint8_t)m68k_read8(addr);
      return 0xff;
    }
  }
}

static void z80_write(uint16_t a, uint8_t v)
{
  switch (a >> 13) {
    case 0: case 1:
      md.zram[a & 0x1fff] = v;
      break;
    case 2:
      md_ym_write(a & 3, v);
      break;
    case 3:
      if ((a >> 8) == 0x60)
        zbank = ((zbank >> 1) | ((uint32_t)(v & 1) << 23)) & 0xff8000;
      else if (a >= 0x7f10 && a < 0x7f18 && (a & 1))
        md_psg_write(v);
      break;
    default: {
      uint32_t addr = zbank | (a & 0x7fff);
      if (addr >= 0xe00000)
        m68k_write8(addr, v);
      break;
    }
  }
}

static void z80_run_to(int64_t t)
{
  while (z80_time < t) {
    if (md.z80_busreq || !md.z80_reset) {
      z80_time = t;
      break;
    }
    z80_pins = z80_tick(&z80, (z80_pins & ~Z80_INT) | (z80_int ? Z80_INT : 0));
    z80_time += Z80_MCYCLES;
    if (z80_pins & Z80_MREQ) {
      uint16_t a = Z80_GET_ADDR(z80_pins);
      if (z80_pins & Z80_RD) {
        uint8_t d = z80_read(a);
        Z80_SET_DATA(z80_pins, d);
      } else if (z80_pins & Z80_WR) {
        z80_write(a, Z80_GET_DATA(z80_pins));
      }
    } else if ((z80_pins & Z80_IORQ) && (z80_pins & Z80_RD)) {
      Z80_SET_DATA(z80_pins, 0xff);
    }
  }
}

/* ---- interface ------------------------------------------------------------ */

void audio_init(void)
{
  OPN2_SetChipType(ym3438_mode_ym2612);
  z80_pins = z80_init(&z80);
  z80_int = 0;
  zbank = 0;
  OPN2_Reset(&ym);
  psg_reset();
  audio_time = z80_time = ym_time = psg_time = 0;
  ym_clock = 0;
  fm_acc[0] = fm_acc[1] = 0;
  out_head = out_count = 0;
}

void audio_run(uint32_t mcycles)
{
  int64_t t = audio_time + mcycles;
  z80_run_to(t);
  ym_run_to(t);
  audio_time = t;
}

void audio_set_z80_int(int asserted)
{
  z80_int = asserted;
}

void audio_z80_reset(void)
{
  ym_run_to(audio_time);
  z80_pins = z80_reset(&z80);
  OPN2_Reset(&ym);
  ym_clock = 0;
  fm_acc[0] = fm_acc[1] = 0;
}

void audio_play_music(int track)
{
  static const uint8_t sound_id[AUDIO_MUSIC_TRACK_COUNT] = {0x82, 0x84, 0x83, 0x85};
  if (track >= 0 && track < AUDIO_MUSIC_TRACK_COUNT)
    md.zram[0x1c03] = sound_id[track];
}

uint64_t audio_mcycles(void)
{
  return (uint64_t)(z80_time > audio_time ? z80_time : audio_time);
}

size_t audio_read(int16_t *dst, size_t max_frames)
{
  size_t n = out_count < max_frames ? out_count : max_frames;
  for (size_t i = 0; i < n; i++) {
    size_t k = (out_head + i) % OUT_CAPACITY;
    dst[i * 2] = out_buf[k * 2];
    dst[i * 2 + 1] = out_buf[k * 2 + 1];
  }
  out_head = (out_head + n) % OUT_CAPACITY;
  out_count -= n;
  return n;
}

void audio_state(StateIO *io)
{
  STATE_VAR(io, z80);
  STATE_VAR(io, z80_pins);
  STATE_VAR(io, z80_int);
  STATE_VAR(io, zbank);
  STATE_VAR(io, ym);
  STATE_VAR(io, psg);
  STATE_VAR(io, audio_time);
  STATE_VAR(io, z80_time);
  STATE_VAR(io, ym_time);
  STATE_VAR(io, psg_time);
  STATE_VAR(io, ym_clock);
  STATE_VAR(io, fm_acc);
  if (io->loading)
    out_head = out_count = 0;
}
