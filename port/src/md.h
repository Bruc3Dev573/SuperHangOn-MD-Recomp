/*
 * Mega Drive hardware seen by the recompiled game: memory map, VDP, I/O.
 */
#ifndef MD_H
#define MD_H

#include <stdint.h>
#include <stddef.h>

#define MD_ROM_MAX 0x400000

typedef struct {
  uint8_t rom[MD_ROM_MAX];
  size_t rom_size;
  uint8_t ram[0x10000];
  uint8_t zram[0x2000];
  uint8_t z80_busreq, z80_reset;
  uint8_t version;          /* $A10001: $A0 = overseas NTSC, no expansion */
  uint8_t pad_ctrl[3], pad_data[3];
  uint16_t pad_buttons[2];  /* active high: bit0 U 1 D 2 L 3 R 4 B 5 C 6 A 7 S */
} MdBus;

typedef struct {
  uint8_t reg[32];
  uint8_t vram[0x10000];
  uint16_t cram[64];
  uint16_t vsram[40];
  int pending;              /* first half of a command word was written */
  uint16_t first;
  uint8_t code;             /* CD5-CD0 */
  uint16_t addr;
  uint8_t dmafill_pending;
  uint16_t status;
  int line;                 /* current raster line (for the HV counter) */
  int hint_counter;
  int vint_pending, hint_pending;
  /* per-line VSRAM snapshot for the renderer (line -> plane A, plane B) */
  uint16_t line_vscroll[240][2];
} MdVdp;

extern MdBus md;
extern MdVdp vdp;

void md_init(void);
int md_load_rom(const char *path);

uint32_t vdp_read_data(void);
uint32_t vdp_read_status(void);
uint32_t vdp_read_hv(void);
void vdp_write_data(uint16_t v);
void vdp_write_ctrl(uint16_t v);

/* YM2612 / PSG writes are forwarded here (audio module) */
void md_ym_write(uint32_t port, uint8_t value);
void md_psg_write(uint8_t value);

#endif
