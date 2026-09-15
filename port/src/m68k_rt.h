/*
 * 68000 runtime for statically recompiled code.
 *
 * The generated C (tools/recomp) keeps the 68000 machine state in an M68K
 * structure and uses the helpers below, which implement the exact flag
 * behaviour of every operation (checked against the Musashi 68000 core).
 *
 * The memory bus is provided by the embedding program:
 *   uint32_t m68k_read8/16/32(uint32_t address);
 *   void     m68k_write8/16/32(uint32_t address, uint32_t value);
 */
#ifndef M68K_RT_H
#define M68K_RT_H

#include <stdint.h>

typedef struct M68K {
  uint32_t d[8];
  uint32_t a[8];          /* a[7] is the active stack pointer */
  uint32_t osp;           /* the inactive stack pointer (USP in supervisor mode) */
  uint32_t pc;            /* address of the instruction being executed */
  uint8_t x, n, z, v, c;  /* condition codes, 0 or 1 */
  uint8_t s, t;           /* supervisor and trace bits */
  uint8_t imask;          /* interrupt mask 0..7 */
  uint8_t stopped;
} M68K;

uint32_t m68k_read8(uint32_t address);
uint32_t m68k_read16(uint32_t address);
uint32_t m68k_read32(uint32_t address);
void m68k_write8(uint32_t address, uint32_t value);
void m68k_write16(uint32_t address, uint32_t value);
void m68k_write32(uint32_t address, uint32_t value);

/* ------------------------------------------------------------ helpers */

#define RT_INLINE static inline __attribute__((always_inline))

RT_INLINE uint32_t sx8(uint32_t v) { return (uint32_t)(int32_t)(int8_t)v; }
RT_INLINE uint32_t sx16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* sizes: 1 = byte, 2 = word, 4 = long */
RT_INLINE uint32_t mask_of(int sz) { return sz == 1 ? 0xff : sz == 2 ? 0xffff : 0xffffffff; }
RT_INLINE uint32_t msb_of(int sz) { return sz == 1 ? 0x80 : sz == 2 ? 0x8000 : 0x80000000; }

/* store the low `sz` bytes of v into register r, keeping the upper bits */
RT_INLINE void setreg(uint32_t *r, uint32_t v, int sz)
{
  uint32_t m = mask_of(sz);
  *r = (*r & ~m) | (v & m);
}

RT_INLINE uint16_t get_ccr(const M68K *c)
{
  return (uint16_t)((c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c);
}

RT_INLINE void set_ccr(M68K *c, uint32_t v)
{
  c->x = (v >> 4) & 1;
  c->n = (v >> 3) & 1;
  c->z = (v >> 2) & 1;
  c->v = (v >> 1) & 1;
  c->c = v & 1;
}

RT_INLINE uint16_t get_sr(const M68K *c)
{
  return (uint16_t)((c->t << 15) | (c->s << 13) | (c->imask << 8) | get_ccr(c));
}

RT_INLINE void set_sr(M68K *c, uint32_t v)
{
  uint8_t s = (v >> 13) & 1;
  if (s != c->s) {
    uint32_t sp = c->a[7];
    c->a[7] = c->osp;
    c->osp = sp;
    c->s = s;
  }
  c->t = (v >> 15) & 1;
  c->imask = (v >> 8) & 7;
  set_ccr(c, v);
}

RT_INLINE uint32_t rd(uint32_t a, int sz)
{
  return sz == 1 ? m68k_read8(a) : sz == 2 ? m68k_read16(a) : m68k_read32(a);
}

RT_INLINE void wr(uint32_t a, uint32_t v, int sz)
{
  if (sz == 1) m68k_write8(a, v & 0xff);
  else if (sz == 2) m68k_write16(a, v & 0xffff);
  else m68k_write32(a, v);
}

/* -------------------------------------------------------- arithmetic */

RT_INLINE void f_nz(M68K *c, uint32_t res, int sz)
{
  res &= mask_of(sz);
  c->n = (res & msb_of(sz)) != 0;
  c->z = res == 0;
}

/* move, and, or, eor, not, tst, ext, swap, moveq, clr */
RT_INLINE uint32_t f_logic(M68K *c, uint32_t res, int sz)
{
  f_nz(c, res, sz);
  c->v = 0;
  c->c = 0;
  return res & mask_of(sz);
}

RT_INLINE uint32_t f_add(M68K *c, uint32_t s, uint32_t d, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  s &= m; d &= m;
  uint64_t r64 = (uint64_t)s + d;
  uint32_t res = (uint32_t)r64 & m;
  c->x = c->c = (r64 >> (sz * 8)) & 1;
  c->v = (((s ^ res) & (d ^ res)) & msb) != 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_sub(M68K *c, uint32_t s, uint32_t d, int sz)  /* d - s */
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  s &= m; d &= m;
  uint32_t res = (d - s) & m;
  c->x = c->c = s > d;
  c->v = (((s ^ d) & (res ^ d)) & msb) != 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE void f_cmp(M68K *c, uint32_t s, uint32_t d, int sz)  /* flags of d - s, X kept */
{
  uint8_t x = c->x;
  f_sub(c, s, d, sz);
  c->x = x;
}

RT_INLINE uint32_t f_addx(M68K *c, uint32_t s, uint32_t d, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  s &= m; d &= m;
  uint64_t r64 = (uint64_t)s + d + c->x;
  uint32_t res = (uint32_t)r64 & m;
  c->x = c->c = (r64 >> (sz * 8)) & 1;
  c->v = (((s ^ res) & (d ^ res)) & msb) != 0;
  c->n = (res & msb) != 0;
  if (res) c->z = 0;
  return res;
}

RT_INLINE uint32_t f_subx(M68K *c, uint32_t s, uint32_t d, int sz)  /* d - s - X */
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  s &= m; d &= m;
  uint64_t r64 = (uint64_t)d - s - c->x;
  uint32_t res = (uint32_t)r64 & m;
  c->x = c->c = (r64 >> 63) & 1;
  c->v = (((s ^ d) & (res ^ d)) & msb) != 0;
  c->n = (res & msb) != 0;
  if (res) c->z = 0;
  return res;
}

RT_INLINE uint32_t f_neg(M68K *c, uint32_t d, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  d &= m;
  uint32_t res = (0 - d) & m;
  c->x = c->c = res != 0;
  c->v = ((d & res) & msb) != 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_negx(M68K *c, uint32_t d, int sz)
{
  return f_subx(c, d, 0, sz);
}

/* ------------------------------------------------------------- BCD */
/* Undefined N/V behaviour follows Musashi. */

RT_INLINE uint32_t f_abcd(M68K *c, uint32_t s, uint32_t d)
{
  s &= 0xff; d &= 0xff;
  uint32_t res = (s & 0x0f) + (d & 0x0f) + c->x;
  uint32_t v = ~res;
  if (res > 9) res += 6;
  res += (s & 0xf0) + (d & 0xf0);
  c->x = c->c = res > 0x99;
  if (c->c) res -= 0xa0;
  v &= res;
  c->v = (v & 0x80) != 0;
  c->n = (res & 0x80) != 0;
  res &= 0xff;
  if (res) c->z = 0;
  return res;
}

RT_INLINE uint32_t f_sbcd(M68K *c, uint32_t s, uint32_t d)  /* d - s - X */
{
  s &= 0xff; d &= 0xff;
  uint32_t res = (d & 0x0f) - (s & 0x0f) - c->x;
  uint32_t v = ~res;
  if (res > 9) res -= 6;
  res += (d & 0xf0) - (s & 0xf0);
  c->x = c->c = res > 0x99;
  if (c->c) res += 0xa0;
  res &= 0xff;
  v &= res;
  c->v = (v & 0x80) != 0;
  c->n = (res & 0x80) != 0;
  if (res) c->z = 0;
  return res;
}

RT_INLINE uint32_t f_nbcd(M68K *c, uint32_t d)
{
  d &= 0xff;
  uint32_t res = (0x9a - d - c->x) & 0xff;
  if (res != 0x9a) {
    uint32_t v = ~res;
    if ((res & 0x0f) == 0x0a) res = (res & 0xf0) + 0x10;
    res &= 0xff;
    v &= res;
    c->v = (v & 0x80) != 0;
    if (res) c->z = 0;
    c->c = c->x = 1;
    c->n = (res & 0x80) != 0;
    return res;
  }
  c->v = 0;
  c->c = c->x = 0;
  c->n = (res & 0x80) != 0;
  return d;   /* destination unchanged */
}

/* ------------------------------------------------------- mul / div */

RT_INLINE uint32_t f_mulu(M68K *c, uint32_t s, uint32_t d)
{
  uint32_t res = (s & 0xffff) * (d & 0xffff);
  f_logic(c, res, 4);
  return res;
}

RT_INLINE uint32_t f_muls(M68K *c, uint32_t s, uint32_t d)
{
  uint32_t res = (uint32_t)((int32_t)(int16_t)s * (int32_t)(int16_t)d);
  f_logic(c, res, 4);
  return res;
}

/* returns 0 ok (result in *d), 1 overflow (d unchanged), 2 divide by zero */
RT_INLINE int f_divu(M68K *c, uint32_t s, uint32_t *d)
{
  s &= 0xffff;
  if (!s) return 2;
  uint32_t q = *d / s, r = *d % s;
  if (q < 0x10000) {
    c->z = q == 0;
    c->n = (q & 0x8000) != 0;
    c->v = c->c = 0;
    *d = (q & 0xffff) | (r << 16);
    return 0;
  }
  c->v = 1;
  return 1;
}

RT_INLINE int f_divs(M68K *c, uint32_t s, uint32_t *d)
{
  int32_t src = (int16_t)s;
  if (!src) return 2;
  if (*d == 0x80000000 && src == -1) {
    c->z = 1; c->n = 0; c->v = 0; c->c = 0;
    *d = 0;
    return 0;
  }
  int32_t q = (int32_t)*d / src, r = (int32_t)*d % src;
  if (q == (int16_t)q) {
    c->z = q == 0;
    c->n = ((uint32_t)q & 0x8000) != 0;
    c->v = c->c = 0;
    *d = ((uint32_t)q & 0xffff) | ((uint32_t)r << 16);
    return 0;
  }
  c->v = 1;
  return 1;
}

/* ---------------------------------------------------------- shifts */
/* count: already reduced (immediate 1..8, register count & 63) */

RT_INLINE uint32_t f_asl(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  uint64_t w = v;
  uint32_t res = cnt >= bits ? 0 : (uint32_t)(w << cnt) & m;
  c->x = c->c = cnt > bits ? 0 : (w >> (bits - cnt)) & 1;
  /* V: the bits shifted through the sign position were not all equal */
  if (cnt >= bits) c->v = v != 0;
  else {
    uint32_t top = (uint32_t)(((uint64_t)m << (bits - cnt - 1)) & m);  /* top cnt+1 bits */
    uint32_t t = v & top;
    c->v = !(t == 0 || t == top);
  }
  f_nz(c, res, sz);
  (void)msb;
  return res;
}

RT_INLINE uint32_t f_lsl(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  uint64_t w = v;
  uint32_t res = cnt >= bits ? 0 : (uint32_t)(w << cnt) & m;
  c->x = c->c = cnt > bits ? 0 : (w >> (bits - cnt)) & 1;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_asr(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  int64_t sv = (v & msb) ? (int64_t)v - ((int64_t)m + 1) : (int64_t)v;
  uint32_t res;
  if (cnt >= bits) {
    res = (v & msb) ? m : 0;
    c->x = c->c = (v & msb) != 0;
  } else {
    res = (uint32_t)(sv >> cnt) & m;
    c->x = c->c = (sv >> (cnt - 1)) & 1;
  }
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_lsr(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  uint32_t res = cnt >= bits ? 0 : v >> cnt;
  c->x = c->c = cnt > bits ? 0 : ((uint64_t)v >> (cnt - 1)) & 1;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_rol(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  unsigned s = cnt % bits;
  uint32_t res = s ? ((v << s) | (v >> (bits - s))) & m : v;
  c->c = res & 1;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_ror(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz), msb = msb_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = 0; return v; }
  unsigned s = cnt % bits;
  uint32_t res = s ? ((v >> s) | (v << (bits - s))) & m : v;
  c->c = (res & msb) != 0;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_roxl(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = c->x; return v; }
  unsigned s = cnt % (bits + 1);
  uint64_t w = ((uint64_t)c->x << bits) | v;           /* bits+1 wide */
  uint64_t wm = ((uint64_t)1 << (bits + 1)) - 1;
  if (s) w = ((w << s) | (w >> (bits + 1 - s))) & wm;
  uint32_t res = (uint32_t)w & m;
  c->x = c->c = (w >> bits) & 1;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

RT_INLINE uint32_t f_roxr(M68K *c, uint32_t v, unsigned cnt, int sz)
{
  uint32_t m = mask_of(sz);
  unsigned bits = sz * 8;
  v &= m;
  if (!cnt) { f_nz(c, v, sz); c->v = 0; c->c = c->x; return v; }
  unsigned s = cnt % (bits + 1);
  uint64_t w = ((uint64_t)c->x << bits) | v;
  uint64_t wm = ((uint64_t)1 << (bits + 1)) - 1;
  if (s) w = ((w >> s) | (w << (bits + 1 - s))) & wm;
  uint32_t res = (uint32_t)w & m;
  c->x = c->c = (w >> bits) & 1;
  c->v = 0;
  f_nz(c, res, sz);
  return res;
}

/* -------------------------------------------------------- conditions */

RT_INLINE int cond(const M68K *c, int cc)
{
  switch (cc) {
    case 0: return 1;
    case 1: return 0;
    case 2: return !c->c && !c->z;       /* hi */
    case 3: return c->c || c->z;         /* ls */
    case 4: return !c->c;                /* cc */
    case 5: return c->c;                 /* cs */
    case 6: return !c->z;                /* ne */
    case 7: return c->z;                 /* eq */
    case 8: return !c->v;                /* vc */
    case 9: return c->v;                 /* vs */
    case 10: return !c->n;               /* pl */
    case 11: return c->n;                /* mi */
    case 12: return c->n == c->v;        /* ge */
    case 13: return c->n != c->v;        /* lt */
    case 14: return !c->z && c->n == c->v;   /* gt */
    default: return c->z || c->n != c->v;    /* le */
  }
}

/* ------------------------------------------------------- exceptions */

/* Group 1/2 exception: push PC and SR, enter supervisor mode, return the
 * handler address. */
RT_INLINE uint32_t m68k_exception(M68K *c, int vector, uint32_t return_pc)
{
  uint16_t sr = get_sr(c);
  c->t = 0;
  if (!c->s) {
    uint32_t sp = c->a[7];
    c->a[7] = c->osp;
    c->osp = sp;
    c->s = 1;
  }
  c->a[7] -= 4;
  m68k_write32(c->a[7], return_pc);
  c->a[7] -= 2;
  m68k_write16(c->a[7], sr);
  return m68k_read32(vector * 4);
}

#endif
