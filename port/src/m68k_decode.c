/*
 * 68000 instruction decoder (see m68k_decode.h). Follows tools/disasm/m68k.py
 * rule for rule, including the encodings it rejects.
 */
#include "m68k_decode.h"

typedef struct {
  const uint8_t *mem;
  uint32_t len;
  M68kInsn *ins;
  int bad;
} Dec;

/* addressing mode classes (the letters of m68k.py) */
enum {
  M_D = 1 << 0, M_A = 1 << 1, M_I = 1 << 2, M_P = 1 << 3, M_M = 1 << 4, M_d = 1 << 5, M_x = 1 << 6,
  M_w = 1 << 7, M_l = 1 << 8, M_p = 1 << 9, M_X = 1 << 10, M_i = 1 << 11
};
#define ALL  (M_D | M_A | M_I | M_P | M_M | M_d | M_x | M_w | M_l | M_p | M_X | M_i)
#define DATA (ALL & ~M_A)
#define CTRL (M_I | M_d | M_x | M_w | M_l | M_p | M_X)
#define DALT (M_D | M_I | M_P | M_M | M_d | M_x | M_w | M_l)
#define MALT (M_I | M_P | M_M | M_d | M_x | M_w | M_l)
#define ALT  (DALT | M_A)

static uint32_t sx8(uint32_t v) { return (uint32_t)(int32_t)(int8_t)v; }
static uint32_t sx16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

static uint32_t w(Dec *d, uint32_t addr)
{
  if (addr + 2 > d->len) {
    d->bad = 1;
    return 0;
  }
  return (uint32_t)(d->mem[addr] << 8 | d->mem[addr + 1]);
}

static uint32_t l(Dec *d, uint32_t addr)
{
  return w(d, addr) << 16 | w(d, addr + 2);
}

static M68kOperand opd(uint8_t mode, uint8_t reg)
{
  M68kOperand o = {0};
  o.mode = mode;
  o.reg = reg;
  return o;
}

/* sz: 1, 2, 4, 0 = none */
static M68kOperand imm(Dec *d, int sz)
{
  M68kInsn *ins = d->ins;
  uint32_t pos = ins->addr + ins->size;
  M68kOperand o = opd(EA_IMM, 0);
  if (sz == 1) {
    uint32_t v = w(d, pos);
    if (v & 0xff00)
      d->bad = 1;
    o.value = v & 0xff;
    ins->size += 2;
  } else if (sz == 2) {
    o.value = w(d, pos);
    ins->size += 2;
  } else if (sz == 4) {
    o.value = l(d, pos);
    ins->size += 4;
  } else {
    d->bad = 1;
  }
  return o;
}

static M68kOperand ea(Dec *d, unsigned mode, unsigned reg, int sz, unsigned allowed)
{
  M68kInsn *ins = d->ins;
  uint32_t pos = ins->addr + ins->size;
  M68kOperand o = {0};
  unsigned code = 0;
  switch (mode) {
    case 0: code = M_D; o = opd(EA_DREG, reg); break;
    case 1: code = M_A; o = opd(EA_AREG, reg); break;
    case 2: code = M_I; o = opd(EA_IND, reg); break;
    case 3: code = M_P; o = opd(EA_POSTINC, reg); break;
    case 4: code = M_M; o = opd(EA_PREDEC, reg); break;
    case 5:
      code = M_d;
      o = opd(EA_DISP, reg);
      o.disp = (int32_t)sx16(w(d, pos));
      ins->size += 2;
      break;
    case 6: {
      code = M_x;
      uint32_t ext = w(d, pos);
      if (ext & 0x0700)
        d->bad = 1;
      o = opd(EA_INDEX, reg);
      o.disp = (int32_t)sx8(ext & 0xff);
      o.xa = (ext & 0x8000) != 0;
      o.xreg = (ext >> 12) & 7;
      o.xlong = (ext & 0x0800) != 0;
      ins->size += 2;
      break;
    }
    default:
      o = opd(EA_NONE, 0);
      if (reg == 0) {
        code = M_w;
        o.mode = EA_ABSW;
        o.target = sx16(w(d, pos));
        ins->size += 2;
      } else if (reg == 1) {
        code = M_l;
        o.mode = EA_ABSL;
        o.target = l(d, pos);
        ins->size += 4;
      } else if (reg == 2) {
        code = M_p;
        o.mode = EA_PCDISP;
        o.target = pos + sx16(w(d, pos));
        ins->size += 2;
      } else if (reg == 3) {
        code = M_X;
        uint32_t ext = w(d, pos);
        if (ext & 0x0700)
          d->bad = 1;
        o.mode = EA_PCINDEX;
        o.target = pos + sx8(ext & 0xff);
        o.xa = (ext & 0x8000) != 0;
        o.xreg = (ext >> 12) & 7;
        o.xlong = (ext & 0x0800) != 0;
        ins->size += 2;
      } else if (reg == 4) {
        code = M_i;
        o = imm(d, sz);
      } else {
        d->bad = 1;
      }
      break;
  }
  if (!(code & allowed))
    d->bad = 1;
  if (code == M_A && sz == 1)
    d->bad = 1;
  /* (m68k.py keeps reg = None for mode 7 operands) */
  if (mode == 7)
    o.reg = 0xff;
  return o;
}

static void set2(M68kInsn *ins, M68kOperand a, M68kOperand b)
{
  ins->o[0] = a;
  ins->o[1] = b;
  ins->nops = 2;
}

static void set1(M68kInsn *ins, M68kOperand a)
{
  ins->o[0] = a;
  ins->nops = 1;
}

static const uint8_t SZ[3] = {1, 2, 4};

static void line0(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned mode = (op >> 3) & 7, reg = op & 7;
  if (op & 0x0100) {
    unsigned dn = (op >> 9) & 7, t = (op >> 6) & 3;
    if (mode == 1) {
      ins->op = OP_MOVEP;
      ins->sz = (t == 0 || t == 2) ? 2 : 4;
      M68kOperand mem = opd(EA_DISP, reg);
      mem.disp = (int32_t)sx16(w(d, ins->addr + 2));
      ins->size += 2;
      if (t < 2) set2(ins, mem, opd(EA_DREG, dn));
      else set2(ins, opd(EA_DREG, dn), mem);
      return;
    }
    ins->op = (uint8_t)(OP_BTST + t);
    ins->sz = 4;
    M68kOperand a = opd(EA_DREG, dn);
    set2(ins, a, ea(d, mode, reg, 1, t == 0 ? DATA : DALT));
    return;
  }
  unsigned sub = (op >> 9) & 7;
  if (sub == 4) {
    unsigned t = (op >> 6) & 3;
    ins->op = (uint8_t)(OP_BTST + t);
    ins->sz = 4;
    uint32_t n = w(d, ins->addr + 2);
    if (n & 0xff00)
      d->bad = 1;
    M68kOperand bit = opd(EA_IMM, 0);
    bit.value = n;
    ins->size += 2;
    unsigned allowed = t == 0 ? (M_D | M_I | M_P | M_M | M_d | M_x | M_w | M_l | M_p | M_X) : DALT;
    set2(ins, bit, ea(d, mode, reg, 1, allowed));
    return;
  }
  static const uint8_t names[8] = {OP_ORI, OP_ANDI, OP_SUBI, OP_ADDI, 0, OP_EORI, OP_CMPI, 0};
  if (!names[sub]) {
    d->bad = 1;
    return;
  }
  ins->op = names[sub];
  unsigned s = (op >> 6) & 3;
  if (s == 3) {
    d->bad = 1;
    return;
  }
  if (mode == 7 && reg == 4) {
    if (sub != 0 && sub != 1 && sub != 5)
      d->bad = 1;
    if (s == 0) {
      ins->sz = 1;
      M68kOperand v = imm(d, 1);
      set2(ins, v, opd(EA_CCR, 0));
    } else if (s == 1) {
      ins->sz = 2;
      M68kOperand v = imm(d, 2);
      set2(ins, v, opd(EA_SR, 0));
    } else {
      d->bad = 1;
    }
    return;
  }
  ins->sz = SZ[s];
  M68kOperand src = imm(d, ins->sz);
  set2(ins, src, ea(d, mode, reg, ins->sz, DALT));
}

static void move(Dec *d, uint32_t op, int sz)
{
  M68kInsn *ins = d->ins;
  unsigned smode = (op >> 3) & 7, sreg = op & 7, dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
  ins->sz = (uint8_t)sz;
  M68kOperand src = ea(d, smode, sreg, sz, ALL);
  if (dmode == 1) {
    if (sz == 1)
      d->bad = 1;
    ins->op = OP_MOVEA;
    set2(ins, src, opd(EA_AREG, dreg));
    return;
  }
  ins->op = OP_MOVE;
  set2(ins, src, ea(d, dmode, dreg, sz, DALT));
}

static uint32_t reverse16(uint32_t v)
{
  uint32_t r = 0;
  for (int i = 0; i < 16; i++)
    if (v & (1u << i))
      r |= 1u << (15 - i);
  return r;
}

static void line4(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned mode = (op >> 3) & 7, reg = op & 7;
  if (op & 0x0100) {
    unsigned r = (op >> 9) & 7, t = (op >> 6) & 3;
    if (t == 3) {
      ins->op = OP_LEA;
      set2(ins, ea(d, mode, reg, 0, CTRL), opd(EA_AREG, r));
      return;
    }
    if (t == 2) {
      ins->op = OP_CHK;
      ins->sz = 2;
      set2(ins, ea(d, mode, reg, 2, DATA), opd(EA_DREG, r));
      return;
    }
    d->bad = 1;
    return;
  }
  unsigned sub = (op >> 8) & 0xf, s = (op >> 6) & 3;
  if (sub == 0 || sub == 2 || sub == 4 || sub == 6) {
    if (s == 3) {
      ins->op = OP_MOVE;
      ins->sz = 2;
      if (sub == 0) {
        M68kOperand sr = opd(EA_SR, 0);
        set2(ins, sr, ea(d, mode, reg, 2, DALT));
      } else if (sub == 2) {
        d->bad = 1;
      } else if (sub == 4) {
        set2(ins, ea(d, mode, reg, 2, DATA), opd(EA_CCR, 0));
      } else {
        set2(ins, ea(d, mode, reg, 2, DATA), opd(EA_SR, 0));
      }
      return;
    }
    ins->op = sub == 0 ? OP_NEGX : sub == 2 ? OP_CLR : sub == 4 ? OP_NEG : OP_NOT;
    ins->sz = SZ[s];
    set1(ins, ea(d, mode, reg, ins->sz, DALT));
    return;
  }
  if (sub == 8) {
    if (s == 0) {
      ins->op = OP_NBCD;
      set1(ins, ea(d, mode, reg, 1, DALT));
      return;
    }
    if (s == 1) {
      if (mode == 0) {
        ins->op = OP_SWAP;
        set1(ins, opd(EA_DREG, reg));
        return;
      }
      if (mode == 1) {
        d->bad = 1;
        return;
      }
      ins->op = OP_PEA;
      set1(ins, ea(d, mode, reg, 0, CTRL));
      return;
    }
    if (mode == 0) {
      ins->op = OP_EXT;
      ins->sz = s == 2 ? 2 : 4;
      ins->ext_long = s != 2;
      set1(ins, opd(EA_DREG, reg));
      return;
    }
    ins->op = OP_MOVEM;
    ins->sz = s == 2 ? 2 : 4;
    uint32_t mask = w(d, ins->addr + 2);
    ins->size += 2;
    if (!mask)
      d->bad = 1;
    if (mode == 4)
      mask = reverse16(mask);
    M68kOperand lst = opd(EA_REGLIST, 0);
    lst.value = mask;
    set2(ins, lst, ea(d, mode, reg, ins->sz, M_I | M_M | M_d | M_x | M_w | M_l));
    return;
  }
  if (sub == 0xA) {
    if (op == 0x4AFC) {
      ins->op = OP_ILLEGAL;
      return;
    }
    if (s == 3) {
      ins->op = OP_TAS;
      set1(ins, ea(d, mode, reg, 1, DALT));
      return;
    }
    ins->op = OP_TST;
    ins->sz = SZ[s];
    set1(ins, ea(d, mode, reg, ins->sz, DALT));
    return;
  }
  if (sub == 0xC) {
    if (s < 2) {
      d->bad = 1;
      return;
    }
    ins->op = OP_MOVEM;
    ins->sz = s == 2 ? 2 : 4;
    uint32_t mask = w(d, ins->addr + 2);
    ins->size += 2;
    if (!mask)
      d->bad = 1;
    M68kOperand src = ea(d, mode, reg, ins->sz, M_I | M_P | M_d | M_x | M_w | M_l | M_p | M_X);
    M68kOperand lst = opd(EA_REGLIST, 0);
    lst.value = mask;
    set2(ins, src, lst);
    return;
  }
  if (sub == 0xE) {
    if (s == 1) {
      unsigned k = (op >> 3) & 7;
      if (k == 0 || k == 1) {
        ins->op = OP_TRAP;
        M68kOperand v = opd(EA_IMM, 0);
        v.value = op & 0xf;
        set1(ins, v);
        return;
      }
      if (k == 2) {
        ins->op = OP_LINK;
        M68kOperand v = opd(EA_IMM, 0);
        v.value = w(d, ins->addr + 2);
        ins->size += 2;
        set2(ins, opd(EA_AREG, reg), v);
        return;
      }
      if (k == 3) {
        ins->op = OP_UNLK;
        set1(ins, opd(EA_AREG, reg));
        return;
      }
      if (k == 4) {
        ins->op = OP_MOVE;
        ins->sz = 4;
        set2(ins, opd(EA_AREG, reg), opd(EA_USP, 0));
        return;
      }
      if (k == 5) {
        ins->op = OP_MOVE;
        ins->sz = 4;
        set2(ins, opd(EA_USP, 0), opd(EA_AREG, reg));
        return;
      }
      switch (op) {
        case 0x4E70: ins->op = OP_RESET; return;
        case 0x4E71: ins->op = OP_NOP; return;
        case 0x4E73: ins->op = OP_RTE; return;
        case 0x4E75: ins->op = OP_RTS; return;
        case 0x4E76: ins->op = OP_TRAPV; return;
        case 0x4E77: ins->op = OP_RTR; return;
        case 0x4E72: ins->op = OP_STOP; set1(ins, imm(d, 2)); return;
        default: d->bad = 1; return;
      }
    }
    if (s == 2 || s == 3) {
      ins->op = s == 2 ? OP_JSR : OP_JMP;
      set1(ins, ea(d, mode, reg, 0, CTRL));
      return;
    }
  }
  d->bad = 1;
}

static void line5(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned mode = (op >> 3) & 7, reg = op & 7, s = (op >> 6) & 3;
  if (s == 3) {
    ins->cond = (op >> 8) & 0xf;
    if (mode == 1) {
      ins->op = OP_DBCC;
      uint32_t pos = ins->addr + 2;
      M68kOperand t = opd(EA_BRANCH, 0);
      t.target = pos + sx16(w(d, pos));
      ins->size += 2;
      set2(ins, opd(EA_DREG, reg), t);
      return;
    }
    ins->op = OP_SCC;
    set1(ins, ea(d, mode, reg, 1, DALT));
    return;
  }
  unsigned data = (op >> 9) & 7;
  ins->op = (op & 0x0100) ? OP_SUBQ : OP_ADDQ;
  ins->sz = SZ[s];
  M68kOperand v = opd(EA_IMM, 0);
  v.value = data ? data : 8;
  set2(ins, v, ea(d, mode, reg, ins->sz, ALT));
}

static void line6(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned cond = (op >> 8) & 0xf, d8 = op & 0xff;
  uint32_t pos = ins->addr + 2;
  M68kOperand t = opd(EA_BRANCH, 0);
  if (d8 == 0) {
    t.target = pos + sx16(w(d, pos));
    ins->size += 2;
    ins->sz = 2;
  } else if (d8 == 0xff) {
    d->bad = 1;
  } else {
    t.target = pos + sx8(d8);
    ins->sz = 1;
  }
  ins->op = cond == 0 ? OP_BRA : cond == 1 ? OP_BSR : OP_BCC;
  ins->cond = (uint8_t)cond;
  set1(ins, t);
}

static void line7(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  if (op & 0x0100)
    d->bad = 1;
  ins->op = OP_MOVEQ;
  M68kOperand v = opd(EA_IMM, 0);
  v.value = sx8(op & 0xff);
  set2(ins, v, opd(EA_DREG, (op >> 9) & 7));
}

/* add/sub/and/or style: returns 0 for size 3 */
static int arith(Dec *d, uint32_t op, uint8_t name, int allow_addr_src, uint8_t xname)
{
  M68kInsn *ins = d->ins;
  unsigned r = (op >> 9) & 7, mode = (op >> 3) & 7, reg = op & 7, s = (op >> 6) & 3, dir = (op >> 8) & 1;
  if (s == 3)
    return 0;
  ins->sz = SZ[s];
  if (dir == 0) {
    ins->op = name;
    M68kOperand src = ea(d, mode, reg, ins->sz, allow_addr_src ? ALL : DATA);
    set2(ins, src, opd(EA_DREG, r));
    return 1;
  }
  if (mode == 0 || mode == 1) {
    if (!xname) {
      d->bad = 1;
      return 1;
    }
    ins->op = xname;
    if (mode == 0) set2(ins, opd(EA_DREG, reg), opd(EA_DREG, r));
    else set2(ins, opd(EA_PREDEC, reg), opd(EA_PREDEC, r));
    return 1;
  }
  ins->op = name;
  M68kOperand dn = opd(EA_DREG, r);
  set2(ins, dn, ea(d, mode, reg, ins->sz, MALT));
  return 1;
}

static void line8(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned s = (op >> 6) & 3, mode = (op >> 3) & 7, reg = op & 7, r = (op >> 9) & 7;
  if (s == 3) {
    ins->op = (op & 0x100) ? OP_DIVS : OP_DIVU;
    ins->sz = 2;
    set2(ins, ea(d, mode, reg, 2, DATA), opd(EA_DREG, r));
    return;
  }
  if ((op & 0x01f0) == 0x0100) {
    ins->op = OP_SBCD;
    ins->sz = 4;
    if (mode == 0) set2(ins, opd(EA_DREG, reg), opd(EA_DREG, r));
    else set2(ins, opd(EA_PREDEC, reg), opd(EA_PREDEC, r));
    return;
  }
  if ((op & 0x0100) && (mode == 0 || mode == 1)) {
    d->bad = 1;
    return;
  }
  arith(d, op, OP_OR, 0, 0);
}

static void addsub(Dec *d, uint32_t op, uint8_t name, uint8_t aname, uint8_t xname)
{
  M68kInsn *ins = d->ins;
  unsigned s = (op >> 6) & 3, mode = (op >> 3) & 7, reg = op & 7, r = (op >> 9) & 7;
  if (s == 3) {
    ins->op = aname;
    ins->sz = (op & 0x100) ? 4 : 2;
    set2(ins, ea(d, mode, reg, ins->sz, ALL), opd(EA_AREG, r));
    return;
  }
  arith(d, op, name, 1, xname);
}

static void lineB(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned s = (op >> 6) & 3, mode = (op >> 3) & 7, reg = op & 7, r = (op >> 9) & 7;
  if (s == 3) {
    ins->op = OP_CMPA;
    ins->sz = (op & 0x100) ? 4 : 2;
    set2(ins, ea(d, mode, reg, ins->sz, ALL), opd(EA_AREG, r));
    return;
  }
  ins->sz = SZ[s];
  if (op & 0x100) {
    if (mode == 1) {
      ins->op = OP_CMPM;
      set2(ins, opd(EA_POSTINC, reg), opd(EA_POSTINC, r));
      return;
    }
    ins->op = OP_EOR;
    M68kOperand dn = opd(EA_DREG, r);
    set2(ins, dn, ea(d, mode, reg, ins->sz, DALT));
    return;
  }
  ins->op = OP_CMP;
  set2(ins, ea(d, mode, reg, ins->sz, ALL), opd(EA_DREG, r));
}

static void lineC(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned s = (op >> 6) & 3, mode = (op >> 3) & 7, reg = op & 7, r = (op >> 9) & 7;
  if (s == 3) {
    ins->op = (op & 0x100) ? OP_MULS : OP_MULU;
    ins->sz = 2;
    set2(ins, ea(d, mode, reg, 2, DATA), opd(EA_DREG, r));
    return;
  }
  if ((op & 0x01f0) == 0x0100) {
    ins->op = OP_ABCD;
    ins->sz = 4;
    if (mode == 0) set2(ins, opd(EA_DREG, reg), opd(EA_DREG, r));
    else set2(ins, opd(EA_PREDEC, reg), opd(EA_PREDEC, r));
    return;
  }
  if ((op & 0x0100) && (mode == 0 || mode == 1)) {
    unsigned k = (op >> 3) & 0x1f;
    ins->op = OP_EXG;
    ins->sz = 4;
    if (k == 0x08) set2(ins, opd(EA_DREG, r), opd(EA_DREG, reg));
    else if (k == 0x09) set2(ins, opd(EA_AREG, r), opd(EA_AREG, reg));
    else if (k == 0x11) set2(ins, opd(EA_DREG, r), opd(EA_AREG, reg));
    else d->bad = 1;
    return;
  }
  arith(d, op, OP_AND, 0, 0);
}

static void lineE(Dec *d, uint32_t op)
{
  M68kInsn *ins = d->ins;
  unsigned s = (op >> 6) & 3, left = (op >> 8) & 1;
  static const uint8_t kinds[4][2] = {{OP_ASR, OP_ASL}, {OP_LSR, OP_LSL}, {OP_ROXR, OP_ROXL}, {OP_ROR, OP_ROL}};
  if (s == 3) {
    unsigned t = (op >> 9) & 7;
    if (t > 3) {
      d->bad = 1;
      return;
    }
    ins->op = kinds[t][left];
    ins->sz = 2;
    set1(ins, ea(d, (op >> 3) & 7, op & 7, 2, MALT));
    return;
  }
  unsigned t = (op >> 3) & 3, c = (op >> 9) & 7;
  ins->op = kinds[t][left];
  ins->sz = SZ[s];
  if (op & 0x20) {
    set2(ins, opd(EA_DREG, c), opd(EA_DREG, op & 7));
  } else {
    M68kOperand v = opd(EA_IMM, 0);
    v.value = c ? c : 8;
    set2(ins, v, opd(EA_DREG, op & 7));
  }
}

int m68k_decode(const uint8_t *mem, uint32_t len, uint32_t addr, M68kInsn *ins)
{
  Dec d = {mem, len, ins, 0};
  *ins = (M68kInsn){0};
  ins->addr = addr;
  ins->size = 2;
  ins->sz = 4;
  uint32_t op = w(&d, addr);
  if (d.bad)
    return -1;
  switch (op >> 12) {
    case 0x0: line0(&d, op); break;
    case 0x1: move(&d, op, 1); break;
    case 0x2: move(&d, op, 4); break;
    case 0x3: move(&d, op, 2); break;
    case 0x4: line4(&d, op); break;
    case 0x5: line5(&d, op); break;
    case 0x6: line6(&d, op); break;
    case 0x7: line7(&d, op); break;
    case 0x8: line8(&d, op); break;
    case 0x9: addsub(&d, op, OP_SUB, OP_SUBA, OP_SUBX); break;
    case 0xB: lineB(&d, op); break;
    case 0xC: lineC(&d, op); break;
    case 0xD: addsub(&d, op, OP_ADD, OP_ADDA, OP_ADDX); break;
    case 0xE: lineE(&d, op); break;
    default: d.bad = 1; break;
  }
  if (addr + ins->size > len)
    d.bad = 1;
  if (d.bad || ins->op == OP_INVALID)
    return -1;
  return 0;
}
