"""
Motorola 68000 instruction decoder.

Decodes one instruction at a time into a structured Instruction and renders it
in vasm (Motorola syntax) form such that `vasmm68k_mot -no-opt -m68000`
reassembles it to the exact same bytes.

Encodings that are legal for the CPU but that an assembler would never produce
(alternate encodings, garbage in unused extension bits, 68010+ opcodes) are
rejected by raising Invalid: the caller must then emit them as raw data.
"""

import struct


class Invalid(Exception):
    pass


COND = ["t", "f", "hi", "ls", "cc", "cs", "ne", "eq",
        "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"]
SZ = {0: "b", 1: "w", 2: "l"}
SZBYTES = {"b": 1, "w": 2, "l": 4}

# Flow kinds
NORMAL, BRANCH, CBRANCH, CALL, JUMP, RETURN, IJUMP, ICALL, STOP = (
    "normal", "branch", "cbranch", "call", "jump", "return", "ijump", "icall", "stop")


def sx8(v):
    return v - 0x100 if v & 0x80 else v


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def sx32(v):
    return v - 0x100000000 if v & 0x80000000 else v


def hexs(v):
    """Signed hex literal."""
    return "-$%x" % -v if v < 0 else "$%x" % v


def regname(kind, n):
    if kind == "a" and n == 7:
        return "sp"
    return "%s%d" % (kind, n)


class Operand:
    """
    mode: dreg areg ind postinc predec disp index absw absl pcdisp pcindex imm
          sr ccr usp reglist
    """
    __slots__ = ("mode", "reg", "disp", "xreg", "xsize", "value", "size",
                 "target", "ext_addr")

    def __init__(self, mode, reg=None, disp=0, xreg=None, xsize=None,
                 value=None, size=None, target=None, ext_addr=None):
        self.mode = mode
        self.reg = reg        # 0..7
        self.disp = disp
        self.xreg = xreg      # ("d"|"a", n)
        self.xsize = xsize    # "w"|"l"
        self.value = value    # imm value (unsigned in size), abs address, reglist mask
        self.size = size      # operand size for imm
        self.target = target  # absolute address for pc-relative / abs modes
        self.ext_addr = ext_addr  # address of the extension word(s) of this operand

    def render(self, label=None, imm_label=None):
        """label: callable(addr) -> str|None used for pc-relative/abs targets;
        imm_label: same for immediates known to be addresses."""
        m = self.mode
        if m == "dreg":
            return regname("d", self.reg)
        if m == "areg":
            return regname("a", self.reg)
        if m == "ind":
            return "(%s)" % regname("a", self.reg)
        if m == "postinc":
            return "(%s)+" % regname("a", self.reg)
        if m == "predec":
            return "-(%s)" % regname("a", self.reg)
        if m == "disp":
            return "(%s,%s)" % (hexs(self.disp), regname("a", self.reg))
        if m == "index":
            return "(%s,%s,%s.%s)" % (hexs(self.disp), regname("a", self.reg),
                                      regname(*self.xreg), self.xsize)
        if m == "absw":
            lab = label(self.target) if label else None
            return "(%s).w" % (lab if lab else "$%x" % self.target)
        if m == "absl":
            lab = label(self.target) if label else None
            return "(%s).l" % (lab if lab else "$%x" % self.target)
        if m == "pcdisp":
            lab = label(self.target) if label else None
            return "(%s,pc)" % (lab if lab else "$%x" % self.target)
        if m == "pcindex":
            lab = label(self.target) if label else None
            return "(%s,pc,%s.%s)" % (lab if lab else "$%x" % self.target,
                                      regname(*self.xreg), self.xsize)
        if m == "branch":
            lab = label(self.target) if label else None
            return lab if lab else "$%x" % self.target
        if m == "imm" and self.size in ("q", "b") and self.ext_addr is None:
            return "#%d" % self.value
        if m == "imm":
            lab = imm_label(self.value) if (imm_label and self.size == "l") else None
            return "#%s" % (lab if lab else "$%x" % self.value)
        if m in ("sr", "ccr", "usp"):
            return m
        if m == "reglist":
            return reglist_str(self.value)
        raise ValueError(m)


def reglist_str(mask):
    """mask bit0=d0 .. bit15=a7 (normal order)."""
    names = [regname("d", i) for i in range(8)] + [regname("a", i) for i in range(8)]
    parts = []
    i = 0
    while i < 16:
        if mask & (1 << i):
            j = i
            while j + 1 < 16 and (mask & (1 << (j + 1))) and (j + 1) != 8:
                j += 1
            parts.append(names[i] if i == j else "%s-%s" % (names[i], names[j]))
            i = j + 1
        else:
            i += 1
    return "/".join(parts)


class Instruction:
    __slots__ = ("addr", "size", "op", "sz", "operands", "flow", "targets",
                 "raw", "cond")

    def __init__(self, addr):
        self.addr = addr
        self.size = 2
        self.op = None
        self.sz = None
        self.operands = []
        self.flow = NORMAL
        self.targets = []
        self.raw = b""
        self.cond = None

    def mnemonic(self):
        return self.op + ("." + self.sz if self.sz else "")

    def render(self, label=None, imm_label=None):
        ops = ",".join(o.render(label, imm_label) for o in self.operands)
        return "%s\t%s" % (self.mnemonic(), ops) if ops else self.mnemonic()

    def refs(self):
        """Absolute addresses referenced by operands (pc-rel, abs)."""
        out = []
        for o in self.operands:
            if o.mode in ("absw", "absl", "pcdisp", "pcindex"):
                out.append((o.mode, o.target))
        return out


class Decoder:
    def __init__(self, mem, base=0):
        """mem: bytes; base: address of mem[0]."""
        self.mem = mem
        self.base = base

    def w(self, addr):
        off = addr - self.base
        if off < 0 or off + 2 > len(self.mem):
            raise Invalid("out of range")
        return (self.mem[off] << 8) | self.mem[off + 1]

    def l(self, addr):
        return (self.w(addr) << 16) | self.w(addr + 2)

    # --- effective address -------------------------------------------------
    def ea(self, ins, mode, reg, size, allowed):
        """Decode EA; allowed is a string of mode letters:
        D=dreg A=areg I=ind P=postinc M=predec d=disp x=index
        w=absw l=absl p=pcdisp X=pcindex i=imm
        """
        pos = ins.addr + ins.size
        if mode == 0:
            code, op = "D", Operand("dreg", reg)
        elif mode == 1:
            code, op = "A", Operand("areg", reg)
        elif mode == 2:
            code, op = "I", Operand("ind", reg)
        elif mode == 3:
            code, op = "P", Operand("postinc", reg)
        elif mode == 4:
            code, op = "M", Operand("predec", reg)
        elif mode == 5:
            code = "d"
            op = Operand("disp", reg, disp=sx16(self.w(pos)), ext_addr=pos)
            ins.size += 2
        elif mode == 6:
            code = "x"
            ext = self.w(pos)
            if ext & 0x0700:
                raise Invalid("non-brief index extension")
            op = Operand("index", reg, disp=sx8(ext & 0xff),
                         xreg=("a" if ext & 0x8000 else "d", (ext >> 12) & 7),
                         xsize="l" if ext & 0x0800 else "w", ext_addr=pos)
            ins.size += 2
        elif mode == 7:
            if reg == 0:
                code = "w"
                v = sx16(self.w(pos)) & 0xffffffff
                op = Operand("absw", target=v, ext_addr=pos)
                ins.size += 2
            elif reg == 1:
                code = "l"
                op = Operand("absl", target=self.l(pos), ext_addr=pos)
                ins.size += 4
            elif reg == 2:
                code = "p"
                op = Operand("pcdisp", target=(pos + sx16(self.w(pos))) & 0xffffffff,
                             ext_addr=pos)
                ins.size += 2
            elif reg == 3:
                code = "X"
                ext = self.w(pos)
                if ext & 0x0700:
                    raise Invalid("non-brief index extension")
                op = Operand("pcindex", target=(pos + sx8(ext & 0xff)) & 0xffffffff,
                             xreg=("a" if ext & 0x8000 else "d", (ext >> 12) & 7),
                             xsize="l" if ext & 0x0800 else "w", ext_addr=pos)
                ins.size += 2
            elif reg == 4:
                code = "i"
                op = self.imm(ins, size)
            else:
                raise Invalid("bad ea mode 7/%d" % reg)
        if code not in allowed:
            raise Invalid("ea mode not allowed")
        if code == "A" and size == "b":
            raise Invalid("byte access to An")
        return op

    def imm(self, ins, size):
        pos = ins.addr + ins.size
        if size == "b":
            v = self.w(pos)
            if v & 0xff00:
                raise Invalid("garbage in byte immediate")
            v &= 0xff
            ins.size += 2
        elif size == "w":
            v = self.w(pos)
            ins.size += 2
        elif size == "l":
            v = self.l(pos)
            ins.size += 4
        else:
            raise Invalid("imm without size")
        return Operand("imm", value=v, size=size, ext_addr=pos)

    # --- main --------------------------------------------------------------
    def decode(self, addr):
        ins = Instruction(addr)
        op = self.w(addr)
        h = op >> 12
        fn = getattr(self, "_line%X" % h)
        fn(ins, op)
        off = addr - self.base
        ins.raw = bytes(self.mem[off:off + ins.size])
        if len(ins.raw) != ins.size:
            raise Invalid("truncated")
        return ins

    # addressing mode groups
    ALL = "DAIPMdxwlpXi"
    DATA = "DIPMdxwlpXi"
    MEM = "IPMdxwlpXi"
    CTRL = "IdxwlpX"
    ALT = "DAIPMdxwl"
    DALT = "DIPMdxwl"
    MALT = "IPMdxwl"
    CALT = "Idxwl"

    def _line0(self, ins, op):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x0100:
            # dynamic bit ops / movep
            dn = (op >> 9) & 7
            t = (op >> 6) & 3
            if mode == 1:
                ins.op = "movep"
                ins.sz = "w" if t in (0, 2) else "l"
                pos = ins.addr + 2
                d = sx16(self.w(pos))
                ins.size += 2
                mem = Operand("disp", reg, disp=d, ext_addr=pos)
                if t < 2:
                    ins.operands = [mem, Operand("dreg", dn)]
                else:
                    ins.operands = [Operand("dreg", dn), mem]
                return
            ins.op = ["btst", "bchg", "bclr", "bset"][t]
            allowed = self.DATA if t == 0 else self.DALT
            ins.operands = [Operand("dreg", dn),
                            self.ea(ins, mode, reg, "b", allowed)]
            return
        sub = (op >> 9) & 7
        if sub == 4:
            # static bit ops
            t = (op >> 6) & 3
            ins.op = ["btst", "bchg", "bclr", "bset"][t]
            n = self.w(ins.addr + 2)
            if n & 0xff00:
                raise Invalid("garbage in bit number")
            bitop = Operand("imm", value=n, size="b", ext_addr=ins.addr + 2)
            ins.size += 2
            allowed = "DIPMdxwlpX" if t == 0 else self.DALT
            ins.operands = [bitop, self.ea(ins, mode, reg, "b", allowed)]
            return
        names = {0: "ori", 1: "andi", 2: "subi", 3: "addi", 5: "eori", 6: "cmpi"}
        if sub not in names:
            raise Invalid("line0 sub 7")
        ins.op = names[sub]
        s = (op >> 6) & 3
        if s == 3:
            raise Invalid("line0 size 3")
        if mode == 7 and reg == 4:
            if sub not in (0, 1, 5):
                raise Invalid("imm to ccr/sr for bad op")
            if s == 0:
                ins.operands = [self.imm(ins, "b"), Operand("ccr")]
                ins.sz = "b"
            elif s == 1:
                ins.operands = [self.imm(ins, "w"), Operand("sr")]
                ins.sz = "w"
            else:
                raise Invalid("imm to sr size l")
            return
        ins.sz = SZ[s]
        src = self.imm(ins, ins.sz)
        ins.operands = [src, self.ea(ins, mode, reg, ins.sz, self.DALT)]

    def _move(self, ins, op, sz):
        smode, sreg = (op >> 3) & 7, op & 7
        dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
        ins.sz = sz
        src = self.ea(ins, smode, sreg, sz, self.ALL)
        if dmode == 1:
            if sz == "b":
                raise Invalid("movea.b")
            ins.op = "movea"
            ins.operands = [src, Operand("areg", dreg)]
            return
        ins.op = "move"
        dst = self.ea(ins, dmode, dreg, sz, self.DALT)
        ins.operands = [src, dst]

    def _line1(self, ins, op):
        self._move(ins, op, "b")

    def _line2(self, ins, op):
        self._move(ins, op, "l")

    def _line3(self, ins, op):
        self._move(ins, op, "w")

    def _line4(self, ins, op):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x0100:
            r = (op >> 9) & 7
            t = (op >> 6) & 3
            if t == 3:
                ins.op = "lea"
                ins.operands = [self.ea(ins, mode, reg, None, self.CTRL),
                                Operand("areg", r)]
                return
            if t == 2:
                ins.op = "chk"
                ins.sz = "w"
                ins.operands = [self.ea(ins, mode, reg, "w", self.DATA),
                                Operand("dreg", r)]
                return
            raise Invalid("line4 chk.l (68020)")
        sub = (op >> 8) & 0xf
        s = (op >> 6) & 3
        if sub in (0x0, 0x2, 0x4, 0x6):
            if s == 3:
                if sub == 0x0:
                    ins.op = "move"
                    ins.sz = "w"
                    ins.operands = [Operand("sr"), self.ea(ins, mode, reg, "w", self.DALT)]
                    return
                if sub == 0x2:
                    raise Invalid("move ccr,ea (68010)")
                if sub == 0x4:
                    ins.op = "move"
                    ins.sz = "w"
                    ins.operands = [self.ea(ins, mode, reg, "w", self.DATA), Operand("ccr")]
                    return
                ins.op = "move"
                ins.sz = "w"
                ins.operands = [self.ea(ins, mode, reg, "w", self.DATA), Operand("sr")]
                ins.flow = NORMAL
                return
            ins.op = {0x0: "negx", 0x2: "clr", 0x4: "neg", 0x6: "not"}[sub]
            ins.sz = SZ[s]
            ins.operands = [self.ea(ins, mode, reg, ins.sz, self.DALT)]
            return
        if sub == 0x8:
            if s == 0:
                ins.op = "nbcd"
                ins.operands = [self.ea(ins, mode, reg, "b", self.DALT)]
                return
            if s == 1:
                if mode == 0:
                    ins.op = "swap"
                    ins.operands = [Operand("dreg", reg)]
                    return
                if mode == 1:
                    raise Invalid("bkpt")
                ins.op = "pea"
                ins.operands = [self.ea(ins, mode, reg, None, self.CTRL)]
                return
            if mode == 0:
                ins.op = "ext"
                ins.sz = "w" if s == 2 else "l"
                ins.operands = [Operand("dreg", reg)]
                return
            # movem reg->mem
            ins.op = "movem"
            ins.sz = "w" if s == 2 else "l"
            mask = self.w(ins.addr + 2)
            ins.size += 2
            if mask == 0:
                raise Invalid("empty movem")
            if mode == 4:
                mask = int("{:016b}".format(mask)[::-1], 2)
            lst = Operand("reglist", value=mask)
            ins.operands = [lst, self.ea(ins, mode, reg, ins.sz, "IMdxwl")]
            return
        if sub == 0xA:
            if op == 0x4AFC:
                ins.op = "illegal"
                ins.flow = STOP
                return
            if s == 3:
                ins.op = "tas"
                ins.operands = [self.ea(ins, mode, reg, "b", self.DALT)]
                return
            ins.op = "tst"
            ins.sz = SZ[s]
            ins.operands = [self.ea(ins, mode, reg, ins.sz, self.DALT)]
            return
        if sub == 0xC:
            if s < 2:
                raise Invalid("mul/div long (68020)")
            ins.op = "movem"
            ins.sz = "w" if s == 2 else "l"
            mask = self.w(ins.addr + 2)
            ins.size += 2
            if mask == 0:
                raise Invalid("empty movem")
            src = self.ea(ins, mode, reg, ins.sz, "IPdxwlpX")
            ins.operands = [src, Operand("reglist", value=mask)]
            return
        if sub == 0xE:
            if s == 1:
                k = (op >> 3) & 7
                if k in (0, 1):
                    ins.op = "trap"
                    ins.operands = [Operand("imm", value=op & 0xf, size="b")]
                    ins.flow = NORMAL
                    return
                if k == 2:
                    ins.op = "link"
                    d = self.w(ins.addr + 2)
                    ins.size += 2
                    ins.operands = [Operand("areg", reg),
                                    Operand("imm", value=d, size="w", ext_addr=ins.addr + 2)]
                    return
                if k == 3:
                    ins.op = "unlk"
                    ins.operands = [Operand("areg", reg)]
                    return
                if k == 4:
                    ins.op = "move"
                    ins.sz = "l"
                    ins.operands = [Operand("areg", reg), Operand("usp")]
                    return
                if k == 5:
                    ins.op = "move"
                    ins.sz = "l"
                    ins.operands = [Operand("usp"), Operand("areg", reg)]
                    return
                simple = {0x4E70: ("reset", NORMAL), 0x4E71: ("nop", NORMAL),
                          0x4E73: ("rte", RETURN), 0x4E75: ("rts", RETURN),
                          0x4E76: ("trapv", NORMAL), 0x4E77: ("rtr", RETURN)}
                if op in simple:
                    ins.op, ins.flow = simple[op]
                    return
                if op == 0x4E72:
                    ins.op = "stop"
                    ins.operands = [self.imm(ins, "w")]
                    return
                raise Invalid("line4E misc")
            if s == 2 or s == 3:
                ins.op = "jsr" if s == 2 else "jmp"
                dst = self.ea(ins, mode, reg, None, self.CTRL)
                ins.operands = [dst]
                if dst.mode in ("absw", "absl", "pcdisp"):
                    ins.flow = CALL if s == 2 else JUMP
                    ins.targets = [dst.target]
                else:
                    ins.flow = ICALL if s == 2 else IJUMP
                return
        raise Invalid("line4 unknown")

    def _line5(self, ins, op):
        mode, reg = (op >> 3) & 7, op & 7
        s = (op >> 6) & 3
        if s == 3:
            cond = (op >> 8) & 0xf
            if mode == 1:
                ins.op = "db" + COND[cond]
                pos = ins.addr + 2
                tgt = (pos + sx16(self.w(pos))) & 0xffffffff
                ins.size += 2
                ins.operands = [Operand("dreg", reg), Operand("pcdisp", target=tgt, ext_addr=pos)]
                ins.operands[1].mode = "branch"
                ins.flow = CBRANCH
                ins.targets = [tgt]
                return
            ins.op = "s" + COND[cond]
            ins.operands = [self.ea(ins, mode, reg, "b", self.DALT)]
            return
        data = (op >> 9) & 7
        ins.op = "subq" if op & 0x0100 else "addq"
        ins.sz = SZ[s]
        imm = Operand("imm", value=data if data else 8, size="b")
        ins.operands = [imm, self.ea(ins, mode, reg, ins.sz, self.ALT)]

    def _line6(self, ins, op):
        cond = (op >> 8) & 0xf
        d8 = op & 0xff
        pos = ins.addr + 2
        name = {0: "bra", 1: "bsr"}.get(cond, "b" + COND[cond])
        if d8 == 0:
            tgt = (pos + sx16(self.w(pos))) & 0xffffffff
            ins.size += 2
            ins.sz = "w"
        elif d8 == 0xff:
            raise Invalid("bcc.l (68020)")
        else:
            tgt = (pos + sx8(d8)) & 0xffffffff
            ins.sz = "s"
        ins.op = name
        o = Operand("branch", target=tgt, ext_addr=pos if d8 == 0 else None)
        ins.operands = [o]
        ins.targets = [tgt]
        ins.flow = BRANCH if cond == 0 else CALL if cond == 1 else CBRANCH
        ins.cond = cond

    def _line7(self, ins, op):
        if op & 0x0100:
            raise Invalid("moveq bit8")
        ins.op = "moveq"
        ins.operands = [Operand("imm", value=sx8(op & 0xff), size="q"),
                        Operand("dreg", (op >> 9) & 7)]

    def _arith(self, ins, op, name, allow_addr_src, xname):
        """add/sub/and/or/cmp style lines."""
        r = (op >> 9) & 7
        mode, reg = (op >> 3) & 7, op & 7
        s = (op >> 6) & 3
        d = (op >> 8) & 1
        if s == 3:
            return False
        ins.sz = SZ[s]
        if d == 0:
            ins.op = name
            src = self.ea(ins, mode, reg, ins.sz,
                          self.ALL if allow_addr_src else self.DATA)
            ins.operands = [src, Operand("dreg", r)]
            return True
        if mode in (0, 1):
            if xname is None:
                raise Invalid("alt encoding")
            ins.op = xname
            if mode == 0:
                ins.operands = [Operand("dreg", reg), Operand("dreg", r)]
            else:
                ins.operands = [Operand("predec", reg), Operand("predec", r)]
            return True
        ins.op = name
        ins.operands = [Operand("dreg", r), self.ea(ins, mode, reg, ins.sz, self.MALT)]
        return True

    def _line8(self, ins, op):
        s = (op >> 6) & 3
        mode, reg = (op >> 3) & 7, op & 7
        r = (op >> 9) & 7
        if s == 3:
            ins.op = "divs" if op & 0x100 else "divu"
            ins.sz = "w"
            ins.operands = [self.ea(ins, mode, reg, "w", self.DATA), Operand("dreg", r)]
            return
        if (op & 0x01f0) == 0x0100:
            ins.op = "sbcd"
            ins.operands = ([Operand("dreg", reg), Operand("dreg", r)] if mode == 0 else
                            [Operand("predec", reg), Operand("predec", r)])
            return
        if (op & 0x0100) and mode in (0, 1):
            raise Invalid("or alt encoding")
        self._arith(ins, op, "or", False, None)

    def _line9(self, ins, op):
        self._addsub(ins, op, "sub", "suba", "subx")

    def _lineD(self, ins, op):
        self._addsub(ins, op, "add", "adda", "addx")

    def _addsub(self, ins, op, name, aname, xname):
        s = (op >> 6) & 3
        mode, reg = (op >> 3) & 7, op & 7
        r = (op >> 9) & 7
        if s == 3:
            ins.op = aname
            ins.sz = "l" if op & 0x100 else "w"
            ins.operands = [self.ea(ins, mode, reg, ins.sz, self.ALL), Operand("areg", r)]
            return
        self._arith(ins, op, name, True, xname)

    def _lineB(self, ins, op):
        s = (op >> 6) & 3
        mode, reg = (op >> 3) & 7, op & 7
        r = (op >> 9) & 7
        if s == 3:
            ins.op = "cmpa"
            ins.sz = "l" if op & 0x100 else "w"
            ins.operands = [self.ea(ins, mode, reg, ins.sz, self.ALL), Operand("areg", r)]
            return
        ins.sz = SZ[s]
        if op & 0x100:
            if mode == 1:
                ins.op = "cmpm"
                ins.operands = [Operand("postinc", reg), Operand("postinc", r)]
                return
            ins.op = "eor"
            ins.operands = [Operand("dreg", r), self.ea(ins, mode, reg, ins.sz, self.DALT)]
            return
        ins.op = "cmp"
        ins.operands = [self.ea(ins, mode, reg, ins.sz, self.ALL), Operand("dreg", r)]

    def _lineC(self, ins, op):
        s = (op >> 6) & 3
        mode, reg = (op >> 3) & 7, op & 7
        r = (op >> 9) & 7
        if s == 3:
            ins.op = "muls" if op & 0x100 else "mulu"
            ins.sz = "w"
            ins.operands = [self.ea(ins, mode, reg, "w", self.DATA), Operand("dreg", r)]
            return
        if (op & 0x01f0) == 0x0100:
            ins.op = "abcd"
            ins.operands = ([Operand("dreg", reg), Operand("dreg", r)] if mode == 0 else
                            [Operand("predec", reg), Operand("predec", r)])
            return
        if (op & 0x0100) and mode in (0, 1):
            k = (op >> 3) & 0x1f
            if k == 0x08:
                ins.op = "exg"
                ins.operands = [Operand("dreg", r), Operand("dreg", reg)]
                return
            if k == 0x09:
                ins.op = "exg"
                ins.operands = [Operand("areg", r), Operand("areg", reg)]
                return
            if k == 0x11:
                ins.op = "exg"
                ins.operands = [Operand("dreg", r), Operand("areg", reg)]
                return
            raise Invalid("and alt encoding / bad exg")
        self._arith(ins, op, "and", False, None)

    def _lineE(self, ins, op):
        s = (op >> 6) & 3
        left = (op >> 8) & 1
        kinds = ["as", "ls", "rox", "ro"]
        if s == 3:
            t = (op >> 9) & 7
            if t > 3:
                raise Invalid("bitfield (68020)")
            mode, reg = (op >> 3) & 7, op & 7
            ins.op = kinds[t] + ("l" if left else "r")
            ins.sz = "w"
            ins.operands = [self.ea(ins, mode, reg, "w", self.MALT)]
            return
        t = (op >> 3) & 3
        ins.op = kinds[t] + ("l" if left else "r")
        ins.sz = SZ[s]
        c = (op >> 9) & 7
        if op & 0x20:
            ins.operands = [Operand("dreg", c), Operand("dreg", op & 7)]
        else:
            ins.operands = [Operand("imm", value=c if c else 8, size="b"),
                            Operand("dreg", op & 7)]

    def _lineA(self, ins, op):
        raise Invalid("line A")

    def _lineF(self, ins, op):
        raise Invalid("line F")
