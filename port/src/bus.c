/*
 * 68000 memory map of the Mega Drive as used by the game.
 */
#include <stdio.h>
#include <string.h>
#include "md.h"
#include "m68k_rt.h"
#include "audio.h"
#include "state.h"

MdBus md;

#ifdef RT_HOOKS
void (*bus_ram_write_hook)(uint32_t offset, int bytes);
void (*bus_ram_read_hook)(uint32_t offset, int bytes);
#define RAM_WRITE_HOOK(a, n) do { if (bus_ram_write_hook) bus_ram_write_hook((a) & 0xffff, (n)); } while (0)
#define RAM_READ_HOOK(a, n) do { if (bus_ram_read_hook) bus_ram_read_hook((a) & 0xffff, (n)); } while (0)
#else
#define RAM_WRITE_HOOK(a, n) ((void)0)
#define RAM_READ_HOOK(a, n) ((void)0)
#endif

void md_init(void)
{
  size_t rs = md.rom_size;
  uint8_t *keep = NULL;
  (void)keep;
  memset(md.ram, 0, sizeof md.ram);
  memset(md.zram, 0, sizeof md.zram);
  md.z80_busreq = 0;
  md.z80_reset = 0;                                  /* held in reset at power on */
  md.version = 0xa0;
  memset(md.pad_ctrl, 0, sizeof md.pad_ctrl);
  memset(md.pad_data, 0, sizeof md.pad_data);
  md.rom_size = rs;
  memset(&vdp, 0, sizeof vdp);
  audio_init();
}

int md_load_rom(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  md.rom_size = fread(md.rom, 1, MD_ROM_MAX, f);
  fclose(f);
  return md.rom_size ? 0 : -1;
}

static void set_z80_reset(int released)
{
  if (!released)
    audio_z80_reset();
  md.z80_reset = released;
}

/* 3-button pad on port n (TH protocol) */
static uint8_t pad_read(int n)
{
  uint8_t th = (md.pad_data[n] & md.pad_ctrl[n]) & 0x40;
  if (!(md.pad_ctrl[n] & 0x40))
    th = 0x40;                                      /* TH as input: pulled up */
  uint16_t b = n < 2 ? md.pad_buttons[n] : 0;
  uint8_t in;
  if (th)
    in = 0x40 | (~b & 0x3f);                        /* 1 C B R L D U */
  else
    in = (~(((b >> 7) & 1) << 5 | ((b >> 6) & 1) << 4) & 0x30) | (~b & 0x03);  /* 0 S A 0 0 D U */
  in &= 0x7f;
  return (md.pad_data[n] & md.pad_ctrl[n]) | (in & ~md.pad_ctrl[n]);
}

static uint32_t io_read8(uint32_t a)
{
  switch (a & 0x1f) {
    case 0x00: case 0x01: return md.version;
    case 0x02: case 0x03: return pad_read(0);
    case 0x04: case 0x05: return pad_read(1);
    case 0x06: case 0x07: return pad_read(2);
    case 0x08: case 0x09: return md.pad_ctrl[0];
    case 0x0a: case 0x0b: return md.pad_ctrl[1];
    case 0x0c: case 0x0d: return md.pad_ctrl[2];
    default: return 0;
  }
}

static void io_write8(uint32_t a, uint32_t v)
{
  switch (a & 0x1f) {
    case 0x03: md.pad_data[0] = v; break;
    case 0x05: md.pad_data[1] = v; break;
    case 0x07: md.pad_data[2] = v; break;
    case 0x09: md.pad_ctrl[0] = v; break;
    case 0x0b: md.pad_ctrl[1] = v; break;
    case 0x0d: md.pad_ctrl[2] = v; break;
    default: break;
  }
}

static uint32_t z80_area_read8(uint32_t a)
{
  uint32_t z = a & 0xffff;
  if (z < 0x4000)
    return md.zram[z & 0x1fff];
  if (z >= 0x4000 && z < 0x6000)
    return 0;                                        /* YM2612 status: never busy */
  return 0xff;
}

static void z80_area_write8(uint32_t a, uint32_t v)
{
  uint32_t z = a & 0xffff;
  if (z < 0x4000)
    md.zram[z & 0x1fff] = v;
  else if (z >= 0x4000 && z < 0x6000)
    md_ym_write(z & 3, v);
  else if (z == 0x7f11 || z == 0x7f13 || z == 0x7f15 || z == 0x7f17)
    md_psg_write(v);
}

uint32_t m68k_read8(uint32_t a)
{
  a &= 0xffffff;
  if (a < 0x400000)
    return a < md.rom_size ? md.rom[a] : 0;
  if (a >= 0xe00000) {
    RAM_READ_HOOK(a, 1);
    return md.ram[a & 0xffff];
  }
  if (a >= 0xa00000 && a < 0xa10000)
    return z80_area_read8(a);
  if (a >= 0xa10000 && a < 0xa10020)
    return io_read8(a);
  if ((a & 0xffff00) == 0xa11100)
    return (a & 1) ? 0 : (md.z80_busreq ? 0 : 1); /* bit 0 of the MSB: 0 = bus granted */
  if (a >= 0xc00000 && a < 0xc00020) {
    uint32_t w;
    switch (a & 0x1e) {
      case 0x00: case 0x02: w = vdp_read_data(); break;
      case 0x04: case 0x06: w = vdp_read_status(); break;
      case 0x08: case 0x0a: case 0x0c: case 0x0e: w = vdp_read_hv(); break;
      default: return 0;
    }
    return (a & 1) ? (w & 0xff) : (w >> 8);
  }
  return 0;
}

uint32_t m68k_read16(uint32_t a)
{
  a &= 0xffffff;
  if (a >= 0xe00000) {
    RAM_READ_HOOK(a, 2);
    return (md.ram[a & 0xffff] << 8) | md.ram[(a + 1) & 0xffff];
  }
  if (a < 0x400000)
    return (m68k_read8(a) << 8) | m68k_read8(a + 1);
  if (a >= 0xc00000 && a < 0xc00020) {
    switch (a & 0x1e) {
      case 0x00: case 0x02: return vdp_read_data();
      case 0x04: case 0x06: return vdp_read_status();
      case 0x08: case 0x0a: case 0x0c: case 0x0e: return vdp_read_hv();
      default: return 0;
    }
  }
  if ((a & 0xffff00) == 0xa11100)
    return md.z80_busreq ? 0x0000 : 0x0100;
  return (m68k_read8(a) << 8) | m68k_read8(a + 1);
}

uint32_t m68k_read32(uint32_t a)
{
  return (m68k_read16(a) << 16) | m68k_read16(a + 2);
}

void m68k_write8(uint32_t a, uint32_t v)
{
  a &= 0xffffff;
  v &= 0xff;
  if (a >= 0xe00000) {
    RAM_WRITE_HOOK(a, 1);
    md.ram[a & 0xffff] = v;
    return;
  }
  if (a >= 0xa00000 && a < 0xa10000) {
    z80_area_write8(a, v);
    return;
  }
  if (a >= 0xa10000 && a < 0xa10020) {
    io_write8(a, v);
    return;
  }
  if ((a & 0xffff00) == 0xa11100) {
    if (!(a & 1)) md.z80_busreq = v & 1;
    return;
  }
  if ((a & 0xffff00) == 0xa11200) {
    if (!(a & 1)) set_z80_reset(v & 1);
    return;
  }
  if (a >= 0xc00010 && a < 0xc00018) {
    md_psg_write(v);
    return;
  }
  if (a >= 0xc00000 && a < 0xc00010) {
    /* byte writes to the VDP ports behave as words with equal bytes */
    uint16_t w = (uint16_t)(v << 8 | v);
    if ((a & 0x1c) == 0x04) vdp_write_ctrl(w);
    else vdp_write_data(w);
  }
}

void m68k_write16(uint32_t a, uint32_t v)
{
  a &= 0xffffff;
  v &= 0xffff;
  if (a >= 0xe00000) {
    RAM_WRITE_HOOK(a, 2);
    md.ram[a & 0xffff] = v >> 8;
    md.ram[(a + 1) & 0xffff] = v & 0xff;
    return;
  }
  if (a >= 0xc00000 && a < 0xc00010) {
    if ((a & 0x1c) == 0x04) vdp_write_ctrl(v);
    else vdp_write_data(v);
    return;
  }
  if ((a & 0xffff00) == 0xa11100) {
    md.z80_busreq = (v >> 8) & 1;
    return;
  }
  if ((a & 0xffff00) == 0xa11200) {
    set_z80_reset((v >> 8) & 1);
    return;
  }
  m68k_write8(a, v >> 8);
  m68k_write8(a + 1, v & 0xff);
}

void m68k_write32(uint32_t a, uint32_t v)
{
  m68k_write16(a, v >> 16);
  m68k_write16(a + 2, v & 0xffff);
}

void md_state(StateIO *io)
{
  size_t from = offsetof(MdBus, ram);
  state_bytes(io, (uint8_t *)&md + from, sizeof md - from);
}
