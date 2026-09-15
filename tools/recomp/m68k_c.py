"""
68000 instruction -> C translation (uses the decoder in tools/disasm/m68k.py
and the helpers in port/src/m68k_rt.h).

translate(ins) returns C statements for one instruction. Control flow is
expressed with two macros the caller defines:
    GOTO(expr)   continue execution at the 68000 address `expr`
    NEXT()       continue with the following instruction
Every path of the generated code ends with one of them.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disasm"))
import m68k  # noqa: E402

SIZE = {"b": 1, "w": 2, "l": 4, None: 4, "s": 1}
COND = {name: i for i, name in enumerate(m68k.COND)}


class Unsupported(Exception):
    pass


def h32(v):
    return "0x%08xu" % (v & 0xFFFFFFFF)


def reg(o):
    return "c->%s[%d]" % ("d" if o.mode == "dreg" else "a", o.reg)


def xreg_expr(o):
    kind, n = o.xreg
    r = "c->%s[%d]" % (kind, n)
    return r if o.xsize == "l" else "sx16(%s)" % r


class Ctx:
    """Collects C lines; creates temporaries."""

    def __init__(self):
        self.lines = []
        self.tmp = 0

    def t(self, prefix="t"):
        self.tmp += 1
        return "%s%d" % (prefix, self.tmp)

    def emit(self, s):
        self.lines.append(s)


def ea_addr(cx, o, sz):
    """Emit side effects and return a C expression (temporary) holding the
    effective address for memory modes."""
    m = o.mode
    a = cx.t("ea")
    step = 2 if (sz == 1 and o.reg == 7) else sz
    if m == "ind":
        cx.emit("uint32_t %s = c->a[%d];" % (a, o.reg))
    elif m == "postinc":
        cx.emit("uint32_t %s = c->a[%d]; c->a[%d] += %d;" % (a, o.reg, o.reg, step))
    elif m == "predec":
        cx.emit("c->a[%d] -= %d; uint32_t %s = c->a[%d];" % (o.reg, step, a, o.reg))
    elif m == "disp":
        cx.emit("uint32_t %s = c->a[%d] + %s;" % (a, o.reg, h32(o.disp)))
    elif m == "index":
        cx.emit("uint32_t %s = c->a[%d] + %s + %s;" % (a, o.reg, h32(o.disp), xreg_expr(o)))
    elif m in ("absw", "absl", "pcdisp"):
        cx.emit("uint32_t %s = %s;" % (a, h32(o.target)))
    elif m == "pcindex":
        cx.emit("uint32_t %s = %s + %s;" % (a, h32(o.target), xreg_expr(o)))
    else:
        raise Unsupported("ea_addr " + m)
    return a


def is_mem(o):
    return o.mode in ("ind", "postinc", "predec", "disp", "index", "absw", "absl", "pcdisp", "pcindex")


def read_src(cx, o, sz):
    """Value of a source operand (side effects emitted), masked to size."""
    m = o.mode
    msk = {1: "0xff", 2: "0xffff", 4: "0xffffffffu"}[sz]
    if m == "dreg" or m == "areg":
        return "(%s & %s)" % (reg(o), msk)
    if m == "imm":
        return h32(o.value & (0xFFFFFFFF if sz == 4 else (1 << (sz * 8)) - 1))
    if is_mem(o):
        a = ea_addr(cx, o, sz)
        v = cx.t("v")
        cx.emit("uint32_t %s = rd(%s, %d);" % (v, a, sz))
        return v
    raise Unsupported("read_src " + m)


class Dest:
    """Location to be read and/or written (memory address evaluated once)."""

    def __init__(self, cx, o, sz):
        self.cx, self.o, self.sz = cx, o, sz
        if o.mode in ("dreg", "areg"):
            self.addr = None
        elif is_mem(o):
            self.addr = ea_addr(cx, o, sz)
        else:
            raise Unsupported("dest " + o.mode)

    def read(self):
        if self.addr is None:
            return "(%s & %s)" % (reg(self.o), {1: "0xff", 2: "0xffff", 4: "0xffffffffu"}[self.sz])
        v = self.cx.t("v")
        self.cx.emit("uint32_t %s = rd(%s, %d);" % (v, self.addr, self.sz))
        return v

    def write(self, val):
        if self.addr is None:
            if self.o.mode == "areg" and self.sz != 4:
                raise Unsupported("byte/word write to An")
            self.cx.emit("setreg(&%s, %s, %d);" % (reg(self.o), val, self.sz))
        else:
            self.cx.emit("wr(%s, %s, %d);" % (self.addr, val, self.sz))


def translate(ins):
    cx = Ctx()
    op, sz_s = ins.op, ins.sz
    ops = ins.operands
    nxt = ins.addr + ins.size
    sz = SIZE.get(sz_s, 4)

    def done():
        cx.emit("NEXT();")

    # ---------------------------------------------------------- data moves
    if op == "move" and ops[0].mode not in ("sr", "ccr", "usp") and ops[1].mode not in ("sr", "ccr", "usp"):
        v = read_src(cx, ops[0], sz)
        r = cx.t("r")
        cx.emit("uint32_t %s = %s;" % (r, v))
        d = Dest(cx, ops[1], sz)
        d.write(r)
        cx.emit("f_logic(c, %s, %d);" % (r, sz))
        done()
    elif op == "movea":
        v = read_src(cx, ops[0], sz)
        cx.emit("c->a[%d] = %s;" % (ops[1].reg, v if sz == 4 else "sx16(%s)" % v))
        done()
    elif op == "moveq":
        cx.emit("c->d[%d] = %s; f_logic(c, c->d[%d], 4);" % (ops[1].reg, h32(ops[0].value), ops[1].reg))
        done()
    elif op == "move" and ops[0].mode == "sr":
        d = Dest(cx, ops[1], 2)
        d.write("get_sr(c)")
        done()
    elif op == "move" and ops[1].mode == "sr":
        v = read_src(cx, ops[0], 2)
        cx.emit("set_sr(c, %s);" % v)
        done()
    elif op == "move" and ops[1].mode == "ccr":
        v = read_src(cx, ops[0], 2)
        cx.emit("set_ccr(c, %s);" % v)
        done()
    elif op == "move" and ops[1].mode == "usp":
        cx.emit("c->osp = c->a[%d];" % ops[0].reg)
        done()
    elif op == "move" and ops[0].mode == "usp":
        cx.emit("c->a[%d] = c->osp;" % ops[1].reg)
        done()
    elif op == "lea":
        a = ea_addr(cx, ops[0], 4)
        cx.emit("c->a[%d] = %s;" % (ops[1].reg, a))
        done()
    elif op == "pea":
        a = ea_addr(cx, ops[0], 4)
        cx.emit("c->a[7] -= 4; m68k_write32(c->a[7], %s);" % a)
        done()
    elif op == "clr":
        d = Dest(cx, ops[0], sz)
        d.write("0")
        cx.emit("c->n = 0; c->z = 1; c->v = 0; c->c = 0;")
        done()
    elif op == "tst":
        v = read_src(cx, ops[0], sz)
        cx.emit("f_logic(c, %s, %d);" % (v, sz))
        done()
    elif op == "tas":
        d = Dest(cx, ops[0], 1)
        v = d.read()
        cx.emit("f_logic(c, %s, 1);" % v)
        d.write("%s | 0x80" % v)
        done()
    elif op == "swap":
        r = "c->d[%d]" % ops[0].reg
        cx.emit("%s = (%s >> 16) | (%s << 16); f_logic(c, %s, 4);" % (r, r, r, r))
        done()
    elif op == "ext":
        r = "c->d[%d]" % ops[0].reg
        if sz_s == "w":
            cx.emit("setreg(&%s, sx8(%s), 2); f_logic(c, %s, 2);" % (r, r, r))
        else:
            cx.emit("%s = sx16(%s); f_logic(c, %s, 4);" % (r, r, r))
        done()
    elif op == "exg":
        a, b = reg(ops[0]), reg(ops[1])
        cx.emit("{ uint32_t e = %s; %s = %s; %s = e; }" % (a, a, b, b))
        done()
    elif op == "movem":
        translate_movem(cx, ins)
        done()

    # ------------------------------------------------------ arithmetic/logic
    elif op == "cmpm":
        src = read_src(cx, ops[0], sz)
        d = read_src(cx, ops[1], sz)
        cx.emit("f_cmp(c, %s, %s, %d);" % (src, d, sz))
        done()
    elif op in ("add", "sub", "and", "or", "eor", "cmp"):
        src = read_src(cx, ops[0], sz)
        if op == "cmp":
            d = read_src(cx, ops[1], sz)
            cx.emit("f_cmp(c, %s, %s, %d);" % (src, d, sz))
        else:
            d = Dest(cx, ops[1], sz)
            dv = d.read()
            r = cx.t("r")
            if op == "add":
                cx.emit("uint32_t %s = f_add(c, %s, %s, %d);" % (r, src, dv, sz))
            elif op == "sub":
                cx.emit("uint32_t %s = f_sub(c, %s, %s, %d);" % (r, src, dv, sz))
            else:
                sym = {"and": "&", "or": "|", "eor": "^"}[op]
                cx.emit("uint32_t %s = f_logic(c, %s %s %s, %d);" % (r, src, sym, dv, sz))
            d.write(r)
        done()
    elif op in ("addi", "subi", "andi", "ori", "eori", "cmpi") and ops[1].mode in ("ccr", "sr"):
        v = h32(ops[0].value)
        get, put = ("get_ccr", "set_ccr") if ops[1].mode == "ccr" else ("get_sr", "set_sr")
        sym = {"andi": "&", "ori": "|", "eori": "^"}[op]
        cx.emit("%s(c, %s(c) %s %s);" % (put, get, sym, v))
        done()
    elif op in ("addi", "subi", "andi", "ori", "eori", "cmpi"):
        src = h32(ops[0].value)
        if op == "cmpi":
            d = read_src(cx, ops[1], sz)
            cx.emit("f_cmp(c, %s, %s, %d);" % (src, d, sz))
        else:
            d = Dest(cx, ops[1], sz)
            dv = d.read()
            r = cx.t("r")
            fn = {"addi": "f_add(c, %s, %s, %d)", "subi": "f_sub(c, %s, %s, %d)"}
            if op in fn:
                cx.emit("uint32_t %s = %s;" % (r, fn[op] % (src, dv, sz)))
            else:
                sym = {"andi": "&", "ori": "|", "eori": "^"}[op]
                cx.emit("uint32_t %s = f_logic(c, %s %s %s, %d);" % (r, src, sym, dv, sz))
            d.write(r)
        done()
    elif op in ("addq", "subq"):
        q = ops[0].value
        if ops[1].mode == "areg":
            cx.emit("c->a[%d] %s= %d;" % (ops[1].reg, "+" if op == "addq" else "-", q))
        else:
            d = Dest(cx, ops[1], sz)
            dv = d.read()
            r = cx.t("r")
            cx.emit("uint32_t %s = f_%s(c, %d, %s, %d);" % (r, "add" if op == "addq" else "sub", q, dv, sz))
            d.write(r)
        done()
    elif op in ("adda", "suba", "cmpa"):
        v = read_src(cx, ops[0], sz)
        if sz == 2:
            v = "sx16(%s)" % v
        if op == "cmpa":
            cx.emit("f_cmp(c, %s, c->a[%d], 4);" % (v, ops[1].reg))
        else:
            cx.emit("c->a[%d] %s= %s;" % (ops[1].reg, "+" if op == "adda" else "-", v))
        done()
    elif op in ("addx", "subx", "abcd", "sbcd"):
        s_o, d_o = ops
        osz = 1 if op in ("abcd", "sbcd") else sz
        if s_o.mode == "dreg":
            src = "(c->d[%d])" % s_o.reg
            d = Dest(cx, d_o, osz)
        else:
            src = read_src(cx, s_o, osz)
            d = Dest(cx, d_o, osz)
        dv = d.read()
        r = cx.t("r")
        if op in ("addx", "subx"):
            cx.emit("uint32_t %s = f_%s(c, %s, %s, %d);" % (r, op, src, dv, osz))
        else:
            cx.emit("uint32_t %s = f_%s(c, %s, %s);" % (r, op, src, dv))
        d.write(r)
        done()
    elif op in ("neg", "negx", "not", "nbcd"):
        osz = 1 if op == "nbcd" else sz
        d = Dest(cx, ops[0], osz)
        dv = d.read()
        r = cx.t("r")
        if op == "not":
            cx.emit("uint32_t %s = f_logic(c, ~%s, %d);" % (r, dv, osz))
        elif op == "nbcd":
            cx.emit("uint32_t %s = f_nbcd(c, %s);" % (r, dv))
        else:
            cx.emit("uint32_t %s = f_%s(c, %s, %d);" % (r, op, dv, osz))
        d.write(r)
        done()
    elif op in ("mulu", "muls"):
        v = read_src(cx, ops[0], 2)
        cx.emit("c->d[%d] = f_%s(c, %s, c->d[%d]);" % (ops[1].reg, op, v, ops[1].reg))
        done()
    elif op in ("divu", "divs"):
        v = read_src(cx, ops[0], 2)
        cx.emit("if (f_%s(c, %s, &c->d[%d]) == 2) GOTO(m68k_exception(c, 5, %s));" % (op, v, ops[1].reg, h32(nxt)))
        done()
    elif op == "chk":
        v = read_src(cx, ops[0], 2)
        cx.emit("{ int32_t s = (int16_t)c->d[%d], b = (int16_t)%s;" % (ops[1].reg, v))
        cx.emit("  c->z = (s & 0xffff) == 0; c->v = 0; c->c = 0;")
        cx.emit("  if (s < 0 || s > b) { c->n = s < 0; GOTO(m68k_exception(c, 6, %s)); } }" % h32(nxt))
        done()

    # ---------------------------------------------------- shifts / rotates
    elif op in ("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr"):
        if len(ops) == 1:
            d = Dest(cx, ops[0], 2)
            dv = d.read()
            r = cx.t("r")
            cx.emit("uint32_t %s = f_%s(c, %s, 1, 2);" % (r, op, dv))
            d.write(r)
        else:
            cnt = str(ops[0].value) if ops[0].mode == "imm" else "(c->d[%d] & 63)" % ops[0].reg
            rr = "c->d[%d]" % ops[1].reg
            cx.emit("setreg(&%s, f_%s(c, %s, %s, %d), %d);" % (rr, op, rr, cnt, sz, sz))
        done()

    # ----------------------------------------------------------- bit ops
    elif op in ("btst", "bchg", "bclr", "bset"):
        bsz = 4 if ops[1].mode == "dreg" else 1
        if ops[0].mode == "imm":
            bit = "%d" % (ops[0].value % (bsz * 8))
        else:
            bit = "(c->d[%d] %% %d)" % (ops[0].reg, bsz * 8)
        if op == "btst":
            v = read_src(cx, ops[1], bsz)
            cx.emit("c->z = ((%s >> %s) & 1) == 0;" % (v, bit))
        else:
            d = Dest(cx, ops[1], bsz)
            dv = d.read()
            cx.emit("c->z = ((%s >> %s) & 1) == 0;" % (dv, bit))
            expr = {"bchg": "%s ^ (1u << %s)", "bclr": "%s & ~(1u << %s)", "bset": "%s | (1u << %s)"}[op]
            d.write(expr % (dv, bit))
        done()

    # -------------------------------------------------------- control flow
    elif op == "bra":
        cx.emit("GOTO(%s);" % h32(ins.targets[0]))
    elif op == "bsr":
        cx.emit("c->a[7] -= 4; m68k_write32(c->a[7], %s); GOTO(%s);" % (h32(nxt), h32(ins.targets[0])))
    elif op.startswith("b") and op[1:] in COND:
        cx.emit("if (cond(c, %d)) GOTO(%s);" % (COND[op[1:]], h32(ins.targets[0])))
        done()
    elif op.startswith("db") and op[2:] in COND:
        r = "c->d[%d]" % ops[0].reg
        cx.emit("if (!cond(c, %d)) { setreg(&%s, %s - 1, 2); if ((%s & 0xffff) != 0xffff) GOTO(%s); }" % (
            COND[op[2:]], r, r, r, h32(ins.targets[0])))
        done()
    elif op.startswith("s") and op[1:] in COND:
        d = Dest(cx, ops[0], 1)
        d.write("cond(c, %d) ? 0xff : 0" % COND[op[1:]])
        done()
    elif op in ("jmp", "jsr"):
        a = ea_addr(cx, ops[0], 4)
        if op == "jsr":
            cx.emit("c->a[7] -= 4; m68k_write32(c->a[7], %s);" % h32(nxt))
        cx.emit("GOTO(%s);" % a)
    elif op == "rts":
        cx.emit("{ uint32_t ra = m68k_read32(c->a[7]); c->a[7] += 4; GOTO(ra); }")
    elif op == "rtr":
        cx.emit("{ set_ccr(c, m68k_read16(c->a[7])); c->a[7] += 2;")
        cx.emit("  uint32_t ra = m68k_read32(c->a[7]); c->a[7] += 4; GOTO(ra); }")
    elif op == "rte":
        cx.emit("{ uint32_t sr = m68k_read16(c->a[7]); c->a[7] += 2;")
        cx.emit("  uint32_t ra = m68k_read32(c->a[7]); c->a[7] += 4;")
        cx.emit("  set_sr(c, sr); GOTO(ra); }")
    elif op == "link":
        r = ops[0].reg
        cx.emit("c->a[7] -= 4; m68k_write32(c->a[7], c->a[%d]); c->a[%d] = c->a[7];" % (r, r))
        cx.emit("c->a[7] += %s;" % h32(m68k.sx16(ops[1].value)))
        done()
    elif op == "unlk":
        r = ops[0].reg
        cx.emit("c->a[7] = c->a[%d]; c->a[%d] = m68k_read32(c->a[7]); c->a[7] += 4;" % (r, r))
        done()
    elif op == "trap":
        cx.emit("GOTO(m68k_exception(c, %d, %s));" % (32 + ops[0].value, h32(nxt)))
    elif op == "trapv":
        cx.emit("if (c->v) GOTO(m68k_exception(c, 7, %s));" % h32(nxt))
        done()
    elif op == "illegal":
        cx.emit("GOTO(m68k_exception(c, 4, %s));" % h32(ins.addr))
    elif op == "nop":
        done()
    elif op == "reset":
        cx.emit("m68k_reset_devices();")
        done()
    elif op == "stop":
        cx.emit("set_sr(c, %s); c->stopped = 1;" % h32(ops[0].value))
        done()
    else:
        raise Unsupported(ins.render())
    return cx.lines


def translate_movem(cx, ins):
    ops = ins.operands
    sz = SIZE[ins.sz]
    if ops[0].mode == "reglist":      # registers -> memory
        mask, dst = ops[0].value, ops[1]
        order = [("d", i) for i in range(8)] + [("a", i) for i in range(8)]
        if dst.mode == "predec":
            # stored from a7 down to d0; the value of the base register written
            # is its value before the instruction (68000)
            cx.emit("{ uint32_t p = c->a[%d];" % dst.reg)
            for kind, i in reversed(order):
                if mask & (1 << ((8 if kind == "a" else 0) + i)):
                    cx.emit("  p -= %d; wr(p, c->%s[%d], %d);" % (sz, kind, i, sz))
            cx.emit("  c->a[%d] = p; }" % dst.reg)
        else:
            a = ea_addr(cx, dst, sz)
            cx.emit("{ uint32_t p = %s;" % a)
            for kind, i in order:
                if mask & (1 << ((8 if kind == "a" else 0) + i)):
                    cx.emit("  wr(p, c->%s[%d], %d); p += %d;" % (kind, i, sz, sz))
            cx.emit("}")
    else:                              # memory -> registers
        src, mask = ops[0], ops[1].value
        order = [("d", i) for i in range(8)] + [("a", i) for i in range(8)]
        if src.mode == "postinc":
            cx.emit("{ uint32_t p = c->a[%d];" % src.reg)
            for kind, i in order:
                if mask & (1 << ((8 if kind == "a" else 0) + i)):
                    val = "rd(p, %d)" % sz
                    if sz == 2:
                        val = "sx16(%s)" % val if kind == "a" else val
                        if kind == "d":
                            cx.emit("  setreg(&c->d[%d], %s, 2); p += 2;" % (i, val))
                            continue
                    cx.emit("  c->%s[%d] = %s; p += %d;" % (kind, i, val, sz))
            # the final address is stored even if the base register was in the list (Musashi)
            cx.emit("  c->a[%d] = p; }" % src.reg)
        else:
            a = ea_addr(cx, src, sz)
            cx.emit("{ uint32_t p = %s;" % a)
            for kind, i in order:
                if mask & (1 << ((8 if kind == "a" else 0) + i)):
                    val = "rd(p, %d)" % sz
                    if sz == 2 and kind == "a":
                        val = "sx16(%s)" % val
                    if sz == 2 and kind == "d":
                        cx.emit("  setreg(&c->d[%d], %s, 2); p += 2;" % (i, val))
                    else:
                        cx.emit("  c->%s[%d] = %s; p += %d;" % (kind, i, val, sz))
            cx.emit("}")
