/*
 * VDP ports: control/data protocol, registers, VRAM/CRAM/VSRAM, DMA.
 * Behaviour follows Genesis Plus GX (the reference used for verification).
 * Rendering is in render.c.
 */
#include <string.h>
#include "md.h"
#include "m68k_rt.h"
#include "state.h"

MdVdp vdp;

static uint16_t fifo_last;
static int dmafill;

static void bus_write(uint16_t data)
{
  fifo_last = data;
  switch (vdp.code & 0x0f) {
    case 0x01: {                                     /* VRAM */
      uint16_t index = vdp.addr & 0xfffe;
      if (vdp.addr & 1)
        data = (uint16_t)((data >> 8) | (data << 8));
      vdp.vram[index] = data >> 8;
      vdp.vram[(uint16_t)(index + 1)] = data & 0xff;
      break;
    }
    case 0x03:                                       /* CRAM */
      vdp.cram[(vdp.addr >> 1) & 0x3f] = data & 0x0eee;
      break;
    case 0x05: {                                     /* VSRAM */
      unsigned i = (vdp.addr >> 1) & 0x3f;
      if (i < 40)
        vdp.vsram[i] = data & 0x07ff;
      break;
    }
    default:
      break;
  }
  vdp.addr += 2;
}

static void dma_68k(void)
{
  uint32_t len = vdp.reg[19] | (vdp.reg[20] << 8);
  if (!len)
    len = 0x10000;
  uint32_t src = ((uint32_t)(vdp.reg[23] & 0x7f) << 17) | ((uint32_t)vdp.reg[22] << 9) | ((uint32_t)vdp.reg[21] << 1);
  while (len--) {
    bus_write((uint16_t)m68k_read16(src));
    src = ((uint32_t)(vdp.reg[23] & 0x7f) << 17) | ((src + 2) & 0x1ffff);
  }
  vdp.reg[19] = vdp.reg[20] = 0;
  vdp.reg[21] = (src >> 1) & 0xff;
  vdp.reg[22] = (src >> 9) & 0xff;
}

static void dma_fill(void)
{
  uint32_t len = vdp.reg[19] | (vdp.reg[20] << 8);
  if (!len)
    len = 0x10000;
  uint8_t data = fifo_last >> 8;
  if ((vdp.code & 0x0f) == 0x01) {
    do {
      vdp.vram[vdp.addr & 0xffff] = data;
      vdp.addr++;
    } while (--len);
  } else {
    /* CRAM / VSRAM fill: the data word is written at every step */
    do {
      bus_write(fifo_last);
    } while (--len);
  }
  vdp.reg[19] = vdp.reg[20] = 0;
}

static void dma_copy(void)
{
  uint32_t len = vdp.reg[19] | (vdp.reg[20] << 8);
  if (!len)
    len = 0x10000;
  uint16_t src = vdp.reg[21] | (vdp.reg[22] << 8);
  do {
    vdp.vram[vdp.addr & 0xffff] = vdp.vram[src];
    src++;
    vdp.addr++;
  } while (--len);
  vdp.reg[19] = vdp.reg[20] = 0;
  vdp.reg[21] = src & 0xff;
  vdp.reg[22] = src >> 8;
}

void vdp_write_ctrl(uint16_t data)
{
  if (!vdp.pending) {
    vdp.addr = (vdp.addr & 0xc000) | (data & 0x3fff);
    vdp.code = (vdp.code & 0x3c) | ((data >> 14) & 3);
    if ((data & 0xc000) == 0x8000) {
      unsigned r = (data >> 8) & 0x1f;
      if (r < 24)
        vdp.reg[r] = data & 0xff;
    } else {
      vdp.pending = 1;
    }
    return;
  }
  vdp.pending = 0;
  vdp.addr = (uint16_t)(((data & 3) << 14) | (vdp.addr & 0x3fff));
  vdp.code = (vdp.code & 0x03) | ((data >> 2) & 0x3c);
  if ((vdp.code & 0x20) && (vdp.reg[1] & 0x10)) {
    switch (vdp.reg[23] >> 6) {
      case 2: dmafill = 1; break;
      case 3: dma_copy(); break;
      default: dma_68k(); break;
    }
  }
}

void vdp_write_data(uint16_t data)
{
  vdp.pending = 0;
  bus_write(data);
  if (dmafill) {
    dmafill = 0;
    dma_fill();
  }
}

uint32_t vdp_read_data(void)
{
  uint16_t data = 0;
  vdp.pending = 0;
  switch (vdp.code & 0x0f) {
    case 0x00: {
      uint16_t index = vdp.addr & 0xfffe;
      data = (uint16_t)((vdp.vram[index] << 8) | vdp.vram[(uint16_t)(index + 1)]);
      if (vdp.addr & 1)
        data = (uint16_t)((data >> 8) | (data << 8));
      break;
    }
    case 0x04: {
      unsigned i = (vdp.addr >> 1) & 0x3f;
      data = i < 40 ? vdp.vsram[i] : 0;
      break;
    }
    case 0x08:
      data = vdp.cram[(vdp.addr >> 1) & 0x3f];
      break;
    default:
      break;
  }
  vdp.addr += 2;
  return data;
}

uint32_t vdp_read_status(void)
{
  vdp.pending = 0;
  uint16_t s = 0x3400 | 0x0200 | vdp.status;         /* FIFO empty, open bus bits */
  if (!(vdp.reg[1] & 0x40))
    s |= 0x0008;                                     /* display off reads as VBlank */
  vdp.status &= ~0x0080;                             /* VINT pending flag cleared on read */
  return s;
}

uint32_t vdp_read_hv(void)
{
  int v = vdp.line;
  if (v > 0xea)
    v -= 6;                                          /* NTSC V counter jump */
  return ((v & 0xff) << 8) | 0x00;
}

void vdp_state(StateIO *io)
{
  STATE_VAR(io, vdp);
  STATE_VAR(io, fifo_last);
  STATE_VAR(io, dmafill);
}
