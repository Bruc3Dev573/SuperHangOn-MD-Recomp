/*
 * Runtime translation of the game code (see rt_translate.h).
 *
 * Every instruction is run with the operations, the order of side effects
 * and the flag helpers (m68k_rt.h) of the C that tools/recomp/m68k_c.py
 * generates for it; blocks, their cycle charge and the wait points are those
 * of tools/recomp/recomp.py, so the game runs exactly as the recompiled build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_translate.h"
#include "m68k_decode.h"
#include "recomp_rt.h"

#define CODE_LIMIT 0x80000
#define CACHE_MAGIC 0x43544853u               /* "SHTC" */
#define CACHE_VERSION 1

typedef struct {
  uint32_t first;       /* index of the first instruction in insns[] */
  uint16_t count;
  uint8_t end;
  uint32_t cost;        /* rt_charge() at the block start */
} Block;

static M68kInsn *insns;
static uint32_t insn_count;
static Block *blocks;
static int32_t block_at[CODE_LIMIT / 2];
static uint8_t wait_at[CODE_LIMIT / 16 + 1];   /* bit per even address */

/* ------------------------------------------------------------- operands */

RT_INLINE uint32_t mask_sz(int sz) { return sz == 4 ? 0xffffffffu : sz == 2 ? 0xffff : 0xff; }

RT_INLINE uint32_t index_reg(const M68K *c, const M68kOperand *o)
{
  uint32_t r = o->xa ? c->a[o->xreg] : c->d[o->xreg];
  return o->xlong ? r : sx16(r);
}

/* effective address of a memory operand, with its side effects */
static uint32_t ea_addr(M68K *c, const M68kOperand *o, int sz)
{
  uint32_t step = (sz == 1 && o->reg == 7) ? 2 : (uint32_t)sz;
  switch (o->mode) {
    case EA_IND: return c->a[o->reg];
    case EA_POSTINC: { uint32_t a = c->a[o->reg]; c->a[o->reg] += step; return a; }
    case EA_PREDEC: c->a[o->reg] -= step; return c->a[o->reg];
    case EA_DISP: return c->a[o->reg] + (uint32_t)o->disp;
    case EA_INDEX: return c->a[o->reg] + (uint32_t)o->disp + index_reg(c, o);
    case EA_ABSW: case EA_ABSL: case EA_PCDISP: return o->target;
    case EA_PCINDEX: return o->target + index_reg(c, o);
    default: return 0;
  }
}

/* value of a source operand, masked to its size */
static uint32_t read_src(M68K *c, const M68kOperand *o, int sz)
{
  switch (o->mode) {
    case EA_DREG: return c->d[o->reg] & mask_sz(sz);
    case EA_AREG: return c->a[o->reg] & mask_sz(sz);
    case EA_IMM: return o->value & mask_sz(sz);
    default: return rd(ea_addr(c, o, sz), sz);
  }
}

/* a location read and written (the address evaluated once) */
typedef struct {
  uint32_t *reg;
  uint32_t addr;
  int sz;
} Dest;

static Dest dest(M68K *c, const M68kOperand *o, int sz)
{
  Dest d = {0, 0, sz};
  if (o->mode == EA_DREG) d.reg = &c->d[o->reg];
  else if (o->mode == EA_AREG) d.reg = &c->a[o->reg];
  else d.addr = ea_addr(c, o, sz);
  return d;
}

RT_INLINE uint32_t dest_read(const Dest *d)
{
  return d->reg ? *d->reg & mask_sz(d->sz) : rd(d->addr, d->sz);
}

RT_INLINE void dest_write(const Dest *d, uint32_t v)
{
  if (d->reg) setreg(d->reg, v, d->sz);
  else wr(d->addr, v, d->sz);
}

/* ---------------------------------------------------------- instructions */

static int is_wait(uint32_t a)
{
  return a < CODE_LIMIT && !(a & 1) && (wait_at[a >> 4] >> ((a >> 1) & 7) & 1);
}

/* a taken branch: a backward branch into a wait loop lets a video frame pass */
RT_INLINE void branch(M68K *c, const M68kInsn *ins, uint32_t t)
{
  if (t <= ins->addr && is_wait(t))
    rt_wait_point(c, t);
}

static void movem(M68K *c, const M68kInsn *ins)
{
  const M68kOperand *o0 = &ins->o[0], *o1 = &ins->o[1];
  int sz = ins->sz;
  if (o0->mode == EA_REGLIST) {                  /* registers -> memory */
    uint32_t mask = o0->value;
    if (o1->mode == EA_PREDEC) {
      /* from a7 down to d0; the base register is written with its value
       * before the instruction */
      uint32_t p = c->a[o1->reg];
      for (int i = 15; i >= 0; i--)
        if (mask & (1u << i)) {
          p -= sz;
          wr(p, i >= 8 ? c->a[i - 8] : c->d[i], sz);
        }
      c->a[o1->reg] = p;
    } else {
      uint32_t p = ea_addr(c, o1, sz);
      for (int i = 0; i < 16; i++)
        if (mask & (1u << i)) {
          wr(p, i >= 8 ? c->a[i - 8] : c->d[i], sz);
          p += sz;
        }
    }
    return;
  }
  uint32_t mask = o1->value;                     /* memory -> registers */
  int post = o0->mode == EA_POSTINC;
  uint32_t p = post ? c->a[o0->reg] : ea_addr(c, o0, sz);
  for (int i = 0; i < 16; i++)
    if (mask & (1u << i)) {
      uint32_t v = rd(p, sz);
      if (i >= 8) c->a[i - 8] = sz == 2 ? sx16(v) : v;
      else if (sz == 2) setreg(&c->d[i], v, 2);
      else c->d[i] = v;
      p += sz;
    }
  if (post)
    c->a[o0->reg] = p;                           /* even when the base register was in the list */
}

/* runs one instruction: returns 0 to continue with the next one, 1 when
 * execution continues at *to */
static int exec(M68K *c, const M68kInsn *ins, uint32_t *to)
{
  const M68kOperand *o0 = &ins->o[0], *o1 = &ins->o[1];
  int sz = ins->sz;
  uint32_t nxt = ins->addr + ins->size;
  switch (ins->op) {
    case OP_MOVE:
      if (o0->mode == EA_SR) {
        Dest d = dest(c, o1, 2);
        dest_write(&d, get_sr(c));
      } else if (o1->mode == EA_SR) {
        set_sr(c, read_src(c, o0, 2));
      } else if (o1->mode == EA_CCR) {
        set_ccr(c, read_src(c, o0, 2));
      } else if (o1->mode == EA_USP) {
        c->osp = c->a[o0->reg];
      } else if (o0->mode == EA_USP) {
        c->a[o1->reg] = c->osp;
      } else {
        uint32_t r = read_src(c, o0, sz);
        Dest d = dest(c, o1, sz);
        dest_write(&d, r);
        f_logic(c, r, sz);
      }
      return 0;
    case OP_MOVEA: {
      uint32_t v = read_src(c, o0, sz);
      c->a[o1->reg] = sz == 4 ? v : sx16(v);
      return 0;
    }
    case OP_MOVEQ:
      c->d[o1->reg] = o0->value;
      f_logic(c, c->d[o1->reg], 4);
      return 0;
    case OP_LEA:
      c->a[o1->reg] = ea_addr(c, o0, 4);
      return 0;
    case OP_PEA: {
      uint32_t a = ea_addr(c, o0, 4);
      c->a[7] -= 4;
      m68k_write32(c->a[7], a);
      return 0;
    }
    case OP_CLR: {
      Dest d = dest(c, o0, sz);
      dest_write(&d, 0);
      c->n = 0; c->z = 1; c->v = 0; c->c = 0;
      return 0;
    }
    case OP_TST:
      f_logic(c, read_src(c, o0, sz), sz);
      return 0;
    case OP_TAS: {
      Dest d = dest(c, o0, 1);
      uint32_t v = dest_read(&d);
      f_logic(c, v, 1);
      dest_write(&d, v | 0x80);
      return 0;
    }
    case OP_SWAP: {
      uint32_t *r = &c->d[o0->reg];
      *r = (*r >> 16) | (*r << 16);
      f_logic(c, *r, 4);
      return 0;
    }
    case OP_EXT: {
      uint32_t *r = &c->d[o0->reg];
      if (!ins->ext_long) { setreg(r, sx8(*r), 2); f_logic(c, *r, 2); }
      else { *r = sx16(*r); f_logic(c, *r, 4); }
      return 0;
    }
    case OP_EXG: {
      uint32_t *a = o0->mode == EA_DREG ? &c->d[o0->reg] : &c->a[o0->reg];
      uint32_t *b = o1->mode == EA_DREG ? &c->d[o1->reg] : &c->a[o1->reg];
      uint32_t e = *a; *a = *b; *b = e;
      return 0;
    }
    case OP_MOVEM:
      movem(c, ins);
      return 0;
    case OP_CMPM: {
      uint32_t s = read_src(c, o0, sz);
      uint32_t d = read_src(c, o1, sz);
      f_cmp(c, s, d, sz);
      return 0;
    }
    case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_EOR: case OP_CMP: {
      uint32_t s = read_src(c, o0, sz);
      if (ins->op == OP_CMP) {
        uint32_t d = read_src(c, o1, sz);
        f_cmp(c, s, d, sz);
        return 0;
      }
      Dest d = dest(c, o1, sz);
      uint32_t dv = dest_read(&d), r;
      switch (ins->op) {
        case OP_ADD: r = f_add(c, s, dv, sz); break;
        case OP_SUB: r = f_sub(c, s, dv, sz); break;
        case OP_AND: r = f_logic(c, s & dv, sz); break;
        case OP_OR: r = f_logic(c, s | dv, sz); break;
        default: r = f_logic(c, s ^ dv, sz); break;
      }
      dest_write(&d, r);
      return 0;
    }
    case OP_ADDI: case OP_SUBI: case OP_ANDI: case OP_ORI: case OP_EORI: case OP_CMPI: {
      uint32_t s = o0->value;
      if (o1->mode == EA_CCR || o1->mode == EA_SR) {
        int ccr = o1->mode == EA_CCR;
        uint32_t cur = ccr ? get_ccr(c) : get_sr(c);
        uint32_t v = ins->op == OP_ANDI ? cur & s : ins->op == OP_ORI ? cur | s : cur ^ s;
        if (ccr) set_ccr(c, v); else set_sr(c, v);
        return 0;
      }
      if (ins->op == OP_CMPI) {
        uint32_t d = read_src(c, o1, sz);
        f_cmp(c, s, d, sz);
        return 0;
      }
      Dest d = dest(c, o1, sz);
      uint32_t dv = dest_read(&d), r;
      switch (ins->op) {
        case OP_ADDI: r = f_add(c, s, dv, sz); break;
        case OP_SUBI: r = f_sub(c, s, dv, sz); break;
        case OP_ANDI: r = f_logic(c, s & dv, sz); break;
        case OP_ORI: r = f_logic(c, s | dv, sz); break;
        default: r = f_logic(c, s ^ dv, sz); break;
      }
      dest_write(&d, r);
      return 0;
    }
    case OP_ADDQ: case OP_SUBQ: {
      uint32_t q = o0->value;
      if (o1->mode == EA_AREG) {
        if (ins->op == OP_ADDQ) c->a[o1->reg] += q; else c->a[o1->reg] -= q;
        return 0;
      }
      Dest d = dest(c, o1, sz);
      uint32_t dv = dest_read(&d);
      dest_write(&d, ins->op == OP_ADDQ ? f_add(c, q, dv, sz) : f_sub(c, q, dv, sz));
      return 0;
    }
    case OP_ADDA: case OP_SUBA: case OP_CMPA: {
      uint32_t v = read_src(c, o0, sz);
      if (sz == 2) v = sx16(v);
      if (ins->op == OP_CMPA) f_cmp(c, v, c->a[o1->reg], 4);
      else if (ins->op == OP_ADDA) c->a[o1->reg] += v;
      else c->a[o1->reg] -= v;
      return 0;
    }
    case OP_ADDX: case OP_SUBX: case OP_ABCD: case OP_SBCD: {
      int osz = (ins->op == OP_ABCD || ins->op == OP_SBCD) ? 1 : sz;
      uint32_t s;
      Dest d;
      if (o0->mode == EA_DREG) {
        s = c->d[o0->reg];
        d = dest(c, o1, osz);
      } else {
        s = read_src(c, o0, osz);
        d = dest(c, o1, osz);
      }
      uint32_t dv = dest_read(&d), r;
      switch (ins->op) {
        case OP_ADDX: r = f_addx(c, s, dv, osz); break;
        case OP_SUBX: r = f_subx(c, s, dv, osz); break;
        case OP_ABCD: r = f_abcd(c, s, dv); break;
        default: r = f_sbcd(c, s, dv); break;
      }
      dest_write(&d, r);
      return 0;
    }
    case OP_NEG: case OP_NEGX: case OP_NOT: case OP_NBCD: {
      int osz = ins->op == OP_NBCD ? 1 : sz;
      Dest d = dest(c, o0, osz);
      uint32_t dv = dest_read(&d), r;
      switch (ins->op) {
        case OP_NOT: r = f_logic(c, ~dv, osz); break;
        case OP_NBCD: r = f_nbcd(c, dv); break;
        case OP_NEG: r = f_neg(c, dv, osz); break;
        default: r = f_negx(c, dv, osz); break;
      }
      dest_write(&d, r);
      return 0;
    }
    case OP_MULU: case OP_MULS: {
      uint32_t v = read_src(c, o0, 2);
      c->d[o1->reg] = ins->op == OP_MULU ? f_mulu(c, v, c->d[o1->reg]) : f_muls(c, v, c->d[o1->reg]);
      return 0;
    }
    case OP_DIVU: case OP_DIVS: {
      uint32_t v = read_src(c, o0, 2);
      int r = ins->op == OP_DIVU ? f_divu(c, v, &c->d[o1->reg]) : f_divs(c, v, &c->d[o1->reg]);
      if (r == 2) {
        *to = m68k_exception(c, 5, nxt);
        return 1;
      }
      return 0;
    }
    case OP_CHK: {
      uint32_t v = read_src(c, o0, 2);
      int32_t s = (int16_t)c->d[o1->reg], b = (int16_t)v;
      c->z = (s & 0xffff) == 0; c->v = 0; c->c = 0;
      if (s < 0 || s > b) {
        c->n = s < 0;
        *to = m68k_exception(c, 6, nxt);
        return 1;
      }
      return 0;
    }
    case OP_ASL: case OP_ASR: case OP_LSL: case OP_LSR:
    case OP_ROL: case OP_ROR: case OP_ROXL: case OP_ROXR: {
      uint32_t (*f)(M68K *, uint32_t, unsigned, int);
      switch (ins->op) {
        case OP_ASL: f = f_asl; break;
        case OP_ASR: f = f_asr; break;
        case OP_LSL: f = f_lsl; break;
        case OP_LSR: f = f_lsr; break;
        case OP_ROL: f = f_rol; break;
        case OP_ROR: f = f_ror; break;
        case OP_ROXL: f = f_roxl; break;
        default: f = f_roxr; break;
      }
      if (ins->nops == 1) {
        Dest d = dest(c, o0, 2);
        uint32_t dv = dest_read(&d);
        dest_write(&d, f(c, dv, 1, 2));
      } else {
        unsigned cnt = o0->mode == EA_IMM ? o0->value : (c->d[o0->reg] & 63);
        uint32_t *r = &c->d[o1->reg];
        setreg(r, f(c, *r, cnt, sz), sz);
      }
      return 0;
    }
    case OP_BTST: case OP_BCHG: case OP_BCLR: case OP_BSET: {
      int bsz = o1->mode == EA_DREG ? 4 : 1;
      unsigned bits = (unsigned)bsz * 8;
      unsigned bit = o0->mode == EA_IMM ? o0->value % bits : c->d[o0->reg] % bits;
      if (ins->op == OP_BTST) {
        uint32_t v = read_src(c, o1, bsz);
        c->z = ((v >> bit) & 1) == 0;
        return 0;
      }
      Dest d = dest(c, o1, bsz);
      uint32_t dv = dest_read(&d);
      c->z = ((dv >> bit) & 1) == 0;
      dest_write(&d, ins->op == OP_BCHG ? dv ^ (1u << bit) : ins->op == OP_BCLR ? dv & ~(1u << bit) : dv | (1u << bit));
      return 0;
    }
    case OP_BRA:
      branch(c, ins, o0->target);
      *to = o0->target;
      return 1;
    case OP_BSR:
      c->a[7] -= 4;
      m68k_write32(c->a[7], nxt);
      branch(c, ins, o0->target);
      *to = o0->target;
      return 1;
    case OP_BCC:
      if (cond(c, ins->cond)) {
        branch(c, ins, o0->target);
        *to = o0->target;
        return 1;
      }
      return 0;
    case OP_DBCC:
      if (!cond(c, ins->cond)) {
        uint32_t *r = &c->d[o0->reg];
        setreg(r, *r - 1, 2);
        if ((*r & 0xffff) != 0xffff) {
          branch(c, ins, o1->target);
          *to = o1->target;
          return 1;
        }
      }
      return 0;
    case OP_SCC: {
      Dest d = dest(c, o0, 1);
      dest_write(&d, cond(c, ins->cond) ? 0xff : 0);
      return 0;
    }
    case OP_JMP: case OP_JSR: {
      uint32_t a = ea_addr(c, o0, 4);
      if (ins->op == OP_JSR) {
        c->a[7] -= 4;
        m68k_write32(c->a[7], nxt);
      }
      *to = a;
      return 1;
    }
    case OP_RTS: {
      uint32_t ra = m68k_read32(c->a[7]);
      c->a[7] += 4;
      *to = ra;
      return 1;
    }
    case OP_RTR: {
      set_ccr(c, m68k_read16(c->a[7]));
      c->a[7] += 2;
      uint32_t ra = m68k_read32(c->a[7]);
      c->a[7] += 4;
      *to = ra;
      return 1;
    }
    case OP_RTE: {
      uint32_t sr = m68k_read16(c->a[7]);
      c->a[7] += 2;
      uint32_t ra = m68k_read32(c->a[7]);
      c->a[7] += 4;
      set_sr(c, sr);
      *to = ra;
      return 1;
    }
    case OP_LINK: {
      unsigned r = o0->reg;
      c->a[7] -= 4;
      m68k_write32(c->a[7], c->a[r]);
      c->a[r] = c->a[7];
      c->a[7] += sx16(o1->value);
      return 0;
    }
    case OP_UNLK: {
      unsigned r = o0->reg;
      c->a[7] = c->a[r];
      c->a[r] = m68k_read32(c->a[7]);
      c->a[7] += 4;
      return 0;
    }
    case OP_TRAP:
      *to = m68k_exception(c, 32 + (int)o0->value, nxt);
      return 1;
    case OP_TRAPV:
      if (c->v) {
        *to = m68k_exception(c, 7, nxt);
        return 1;
      }
      return 0;
    case OP_ILLEGAL:
      *to = m68k_exception(c, 4, ins->addr);
      return 1;
    case OP_NOP:
      return 0;
    case OP_RESET:
      m68k_reset_devices();
      return 0;
    case OP_STOP:
      set_sr(c, o0->value);
      c->stopped = 1;
      return 0;
    default:
      *to = rt_unsupported(c, ins->addr);
      return 1;
  }
}

uint32_t rt_translate_run(M68K *c, uint32_t pc)
{
  pc &= 0xffffff;
  int32_t b = (pc < CODE_LIMIT && !(pc & 1)) ? block_at[pc >> 1] : -1;
  if (b < 0)
    return rt_bad_pc(c, pc);
  const Block *blk = &blocks[b];
  rt_charge(blk->cost);
  const M68kInsn *ins = insns + blk->first, *last = ins + blk->count - 1;
  for (;; ins++) {
    c->pc = ins->addr;
    RT_HOOK(c, ins->addr);
    if (ins == last && blk->end == 2)
      return rt_unsupported(c, ins->addr);
    uint32_t to;
    if (exec(c, ins, &to))
      return to;
    if (ins == last) {
      uint32_t nxt = ins->addr + ins->size;
      return blk->end == 1 ? rt_bad_pc(c, nxt) : nxt;
    }
  }
}

/* ----------------------------------------------------------------- set-up */

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
  while (n--) {
    h ^= *p++;
    h *= 16777619u;
  }
  return h;
}

static uint32_t cycle_cost(const M68kInsn *ins)
{
  uint32_t base = 10;
  switch (ins->op) {
    case OP_DIVS: base = 158; break;
    case OP_DIVU: base = 140; break;
    case OP_MULS: case OP_MULU: base = 70; break;
    case OP_MOVEM: base = 60; break;
    default: break;
  }
  return base + 4 * (uint32_t)(ins->size - 2);
}

typedef struct {
  uint32_t magic, version, insn_size, map_hash, image_hash, count;
} CacheHeader;

static int load_cache(const char *path, const CacheHeader *want)
{
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f)
    return -1;
  CacheHeader h;
  int ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(&h, want, sizeof h) &&
           fread(insns, sizeof *insns, want->count, f) == want->count;
  fclose(f);
  return ok ? 0 : -1;
}

static void save_cache(const char *path, const CacheHeader *h)
{
  FILE *f = path ? fopen(path, "wb") : NULL;
  if (!f)
    return;
  int ok = fwrite(h, sizeof *h, 1, f) == 1 && fwrite(insns, sizeof *insns, h->count, f) == h->count;
  if (fclose(f) != 0 || !ok)
    remove(path);
}

static const RtCodeSet *code_set;

int rt_translate_block_count(void)
{
  return code_set ? code_set->block_count : 0;
}

uint32_t rt_translate_block_addr(int i)
{
  return code_set->blocks[i].addr;
}

int rt_translate_init(const RtCodeSet *set, const uint8_t *image, uint32_t size, const char *cache_path)
{
  const RtCodeBlock *rt_code_map = set->blocks;
  const uint32_t *rt_code_waits = set->waits;
  int rt_code_map_count = set->block_count, rt_code_wait_count = set->wait_count;
  code_set = set;
  free(insns);                        /* a set decoded before (the rate changed) */
  free(blocks);
  uint32_t total = 0;
  for (int i = 0; i < rt_code_map_count; i++)
    total += rt_code_map[i].count;
  insns = calloc(total ? total : 1, sizeof *insns);
  blocks = calloc((size_t)rt_code_map_count + 1, sizeof *blocks);
  if (!insns || !blocks)
    return -1;
  insn_count = total;

  uint32_t map_hash = fnv(2166136261u, (const uint8_t *)rt_code_map, sizeof *rt_code_map * (size_t)rt_code_map_count);
  map_hash = fnv(map_hash, (const uint8_t *)rt_code_waits, sizeof *rt_code_waits * (size_t)rt_code_wait_count);
  uint32_t code_size = size < CODE_LIMIT ? size : CODE_LIMIT;
  CacheHeader want = {CACHE_MAGIC, CACHE_VERSION, sizeof *insns, map_hash, fnv(2166136261u, image, code_size), total};
  int cached = load_cache(cache_path, &want) == 0;

  memset(block_at, 0xff, sizeof block_at);
  memset(wait_at, 0, sizeof wait_at);
  for (int i = 0; i < rt_code_wait_count; i++)
    if (rt_code_waits[i] < CODE_LIMIT)
      wait_at[rt_code_waits[i] >> 4] |= (uint8_t)(1 << ((rt_code_waits[i] >> 1) & 7));

  uint32_t n = 0;
  for (int i = 0; i < rt_code_map_count; i++) {
    const RtCodeBlock *m = &rt_code_map[i];
    Block *b = &blocks[i];
    b->first = n;
    b->count = m->count;
    b->end = m->end;
    uint32_t p = m->addr;
    for (int k = 0; k < m->count; k++, n++) {
      M68kInsn *ins = &insns[n];
      if (!cached) {
        if (m68k_decode(image, code_size, p, ins) != 0) {
          if (m->end == 2 && k == m->count - 1) {  /* the recompiler could not translate it either */
            *ins = (M68kInsn){.addr = p, .size = 2};
          } else {
            fprintf(stderr, "rt_translate: no instruction at $%06x\n", p);
            return -1;
          }
        }
      }
      b->cost += cycle_cost(ins);
      p = ins->addr + ins->size;
    }
    if (m->addr < CODE_LIMIT && !(m->addr & 1))
      block_at[m->addr >> 1] = i;
  }
  if (!cached)
    save_cache(cache_path, &want);
  return 0;
}
