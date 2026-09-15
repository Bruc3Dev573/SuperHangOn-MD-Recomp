/*
 * 68000 instruction decoder for the runtime translator (the same decoding as
 * tools/disasm/m68k.py, which the static recompiler uses).
 */
#ifndef M68K_DECODE_H
#define M68K_DECODE_H

#include <stdint.h>

enum {
  EA_NONE, EA_DREG, EA_AREG, EA_IND, EA_POSTINC, EA_PREDEC, EA_DISP, EA_INDEX,
  EA_ABSW, EA_ABSL, EA_PCDISP, EA_PCINDEX, EA_IMM, EA_SR, EA_CCR, EA_USP, EA_REGLIST, EA_BRANCH
};

enum {
  OP_INVALID,
  OP_MOVE, OP_MOVEA, OP_MOVEQ, OP_MOVEP, OP_LEA, OP_PEA, OP_CLR, OP_TST, OP_TAS, OP_SWAP, OP_EXT,
  OP_EXG, OP_MOVEM, OP_CMPM, OP_ADD, OP_SUB, OP_AND, OP_OR, OP_EOR, OP_CMP, OP_ADDI, OP_SUBI,
  OP_ANDI, OP_ORI, OP_EORI, OP_CMPI, OP_ADDQ, OP_SUBQ, OP_ADDA, OP_SUBA, OP_CMPA, OP_ADDX,
  OP_SUBX, OP_ABCD, OP_SBCD, OP_NEG, OP_NEGX, OP_NOT, OP_NBCD, OP_MULU, OP_MULS, OP_DIVU,
  OP_DIVS, OP_CHK, OP_ASL, OP_ASR, OP_LSL, OP_LSR, OP_ROL, OP_ROR, OP_ROXL, OP_ROXR, OP_BTST,
  OP_BCHG, OP_BCLR, OP_BSET, OP_BRA, OP_BSR, OP_BCC, OP_DBCC, OP_SCC, OP_JMP, OP_JSR, OP_RTS,
  OP_RTR, OP_RTE, OP_LINK, OP_UNLK, OP_TRAP, OP_TRAPV, OP_ILLEGAL, OP_NOP, OP_RESET, OP_STOP
};

typedef struct {
  uint8_t mode;
  uint8_t reg;          /* 0..7 */
  uint8_t xa;           /* index register: 1 = An, 0 = Dn */
  uint8_t xreg;
  uint8_t xlong;        /* index register used as a long (else sign-extended word) */
  int32_t disp;
  uint32_t value;       /* immediate (unsigned in its size; moveq sign-extended), reglist mask */
  uint32_t target;      /* address of absolute, pc-relative and branch operands */
} M68kOperand;

typedef struct {
  uint32_t addr;
  uint8_t size;         /* bytes */
  uint8_t op;
  uint8_t sz;           /* operation size in bytes: 1, 2, 4 (4 when the instruction has none) */
  uint8_t ext_long;     /* ext.l (ext.w otherwise) */
  uint8_t cond;         /* bcc / dbcc / scc condition */
  uint8_t nops;
  M68kOperand o[2];
} M68kInsn;

/* decodes the instruction at addr of mem[0..len); returns 0, or -1 for an
 * encoding the decoder rejects */
int m68k_decode(const uint8_t *mem, uint32_t len, uint32_t addr, M68kInsn *ins);

#endif
