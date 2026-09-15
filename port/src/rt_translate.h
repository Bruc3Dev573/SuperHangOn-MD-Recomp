/*
 * Runtime translation of the game code: at start-up the 68000 instructions of
 * the user's ROM (with the source overlays' bytes applied) are decoded into
 * blocks, following a code map that holds only the block structure (address,
 * instruction count, how the block ends) and the wait loop addresses. The
 * decoded blocks are kept in a cache file and run by rt_translate_run() with
 * the semantics of the static recompiler (tools/recomp/m68k_c.py).
 */
#ifndef RT_TRANSLATE_H
#define RT_TRANSLATE_H

#include <stdint.h>
#include "m68k_rt.h"
#include "rompatch.h"

typedef struct {
  uint32_t addr;
  uint16_t count;       /* instructions */
  uint8_t end;          /* 0: after the last instruction, 1: no instruction at the next
                           address (rt_bad_pc), 2: the last instruction is unsupported */
} RtCodeBlock;

/* the game code of one build of the overlays (port/data/SET/) */
typedef struct {
  const RtCodeBlock *blocks;
  int block_count;
  const uint32_t *waits;
  int wait_count;
  const RomPatch *patches;       /* the overlays' bytes, applied to the ROM first */
  const int *patch_count;
} RtCodeSet;

/* decodes the blocks of set from the code image (0 .. size, the ROM with the
 * set's patches applied), or loads them from cache_path when that file was
 * made from the same set and image; writes the cache after decoding.
 * cache_path may be NULL. Returns 0, or -1 when an instruction of the map does
 * not decode (not the expected ROM). */
int rt_translate_init(const RtCodeSet *set, const uint8_t *image, uint32_t size, const char *cache_path);

/* block addresses of the set in use (save state compatibility) */
int rt_translate_block_count(void);
uint32_t rt_translate_block_addr(int i);

/* runs the block at pc; returns the address to continue at */
uint32_t rt_translate_run(M68K *c, uint32_t pc);

#endif
