/* Patched code applied to the original ROM at load (generated rom_patch.c). */
#ifndef ROMPATCH_H
#define ROMPATCH_H
#include <stdint.h>

typedef struct {
  uint32_t addr;
  uint32_t len;
  const uint8_t *bytes;
} RomPatch;

extern const RomPatch rom_patches[];
extern const int rom_patch_count;

#endif
