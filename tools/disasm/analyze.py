"""
Static analysis of the ROM: recursive-descent code discovery driven by the
vector table, symbols.txt and (optionally) dynamic trace coverage.

symbols.txt format (one entry per line, '#' comments):

    <hexaddr> <type> [name] [key=value ...]

types:
    code        code entry point (name optional)
    nocode      address that must never be treated as code
    label       data label
    jt_bra      branch table (sequence of bra.w / bra.s+nop / rts+nop / jmp)
                  count=N   number of entries (default: auto)
    jt_w        table of signed 16-bit offsets, base=<hex> (default: table addr)
                  count=N, code=0 if targets are data (default: code)
    ptr_l       table of 32-bit absolute ROM pointers, count=N,
                  code=0 if targets are data (default: code)
    ram_code    ROM block copied to RAM and executed there:
                  vma=<hex> len=<hex>
    data        data block (bytes), len=<hex> (informative, hard boundary)
    name        just gives a name to an address (any kind)
    resolved    indirect jump/call whose targets were checked by hand
    pin         address that must not move (asserted at assembly time)
    notptr      long at this address is not a pointer (len=<hex> for a range)
    immptr      force the 32-bit immediate of the instruction to be an address
    notimmptr   the 32-bit immediate of the instruction is a plain number
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import m68k  # noqa: E402

ROM_END_DEFAULT = 0x80000


class Symbol:
    def __init__(self, addr, typ, name=None, args=None):
        self.addr = addr
        self.typ = typ
        self.name = name
        self.args = args or {}


def load_symbols(path):
    syms = []
    if not os.path.exists(path):
        return syms
    for ln, line in enumerate(open(path), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        addr = int(parts[0], 16)
        typ = parts[1]
        name = None
        args = {}
        for p in parts[2:]:
            if "=" in p:
                k, v = p.split("=", 1)
                args[k] = v
            else:
                name = p
        syms.append(Symbol(addr, typ, name, args))
    return syms


class Analysis:
    def __init__(self, rom, symbols=(), coverage=()):
        self.rom = rom
        self.dec = m68k.Decoder(rom)
        self.code = {}          # addr -> Instruction
        self.code_owner = {}    # byte offset -> instruction addr (for overlap checks)
        self.names = {}         # addr -> name
        self.labels = set()     # addresses needing a label
        self.data_refs = set()  # addresses referenced as data
        self.tables = {}        # addr -> (kind, entries[list of target addrs], size bytes, base)
        self.ram_blocks = []    # (rom_addr, vma, length)
        self.nocode = set()
        self.errors = []        # (addr, msg)
        self.unresolved = []    # indirect jumps not resolved (addr)
        self.symbols = list(symbols)
        self.coverage = set(coverage)
        self.work = []
        self.entry_src = {}

    # ------------------------------------------------------------------
    def in_rom(self, a):
        return 0 <= a < len(self.rom)

    def add_entry(self, addr, src=None):
        if not self.in_rom(addr) or addr & 1:
            self.errors.append((src if src is not None else addr,
                                "bad code target $%x" % addr))
            return
        self.labels.add(addr)
        if addr not in self.code:
            self.work.append((addr, src))

    def run(self):
        rom = self.rom
        # vectors
        self.names.setdefault(0, "Vectors")
        for i in range(1, 64):
            v = int.from_bytes(rom[i * 4:i * 4 + 4], "big")
            if self.in_rom(v) and v:
                self.add_entry(v, i * 4)
        for s in self.symbols:
            if s.name:
                self.names[s.addr] = s.name
            if s.typ == "code":
                self.add_entry(s.addr, "symbols")
            elif s.typ == "nocode":
                self.nocode.add(s.addr)
            elif s.typ in ("label", "data"):
                self.labels.add(s.addr)
            elif s.typ == "ram_code":
                self.ram_blocks.append((s.addr, int(s.args["vma"], 16),
                                        int(s.args["len"], 16)))
        for a in sorted(self.coverage):
            if self.in_rom(a):
                self.add_entry(a, "trace")
        self.flush()
        for s in self.symbols:
            if s.typ.startswith(("jt_", "ptr_")):
                self.table(s)
        self.flush()

    def flush(self):
        while self.work:
            addr, src = self.work.pop()
            self.trace(addr, src)

    def trace(self, addr, src):
        a = addr
        prev = []
        while True:
            if a in self.code:
                return
            if a in self.nocode:
                self.errors.append((a, "flow into nocode (from $%s)" % (
                    "%x" % src if isinstance(src, int) else src)))
                return
            if not self.in_rom(a):
                self.errors.append((a, "flow out of rom"))
                return
            try:
                ins = self.dec.decode(a)
            except m68k.Invalid as e:
                self.errors.append((a, "invalid instruction (%s) reached from %s" % (
                    e, "$%x" % src if isinstance(src, int) else src)))
                return
            for off in range(a, a + ins.size):
                o = self.code_owner.get(off)
                if o is not None and o != a:
                    self.errors.append((a, "overlaps instruction at $%x" % o))
                    return
            for off in range(a, a + ins.size):
                self.code_owner[off] = a
            self.code[a] = ins
            for mode, t in ins.refs():
                if mode in ("pcdisp", "pcindex") or (mode in ("absl", "absw") and self.in_rom(t)):
                    if self.in_rom(t):
                        self.labels.add(t)
                        if ins.flow not in (m68k.CALL, m68k.JUMP):
                            self.data_refs.add(t)
            f = ins.flow
            if f in (m68k.BRANCH, m68k.JUMP):
                for t in ins.targets:
                    self.add_entry(t, a)
                return
            if f in (m68k.CBRANCH, m68k.CALL):
                for t in ins.targets:
                    self.add_entry(t, a)
            elif f in (m68k.RETURN, m68k.STOP):
                return
            elif f in (m68k.IJUMP, m68k.ICALL):
                if not self.auto_table(ins, prev):
                    self.unresolved.append(a)
                if f == m68k.IJUMP:
                    return
            prev = (prev + [ins])[-8:]
            a += ins.size

    # ------------------------------------------------------------------
    def auto_table(self, ins, prev):
        """Recognise common jump table idioms. Returns True if resolved."""
        dst = ins.operands[0]
        end = ins.addr + ins.size
        base = None
        if dst.mode == "pcindex":
            base = dst.target
            # offset table: move.w (base,pc,dX.w),dY ; jmp (base,pc,dY.w)
            if prev:
                p = prev[-1]
                if (p.op == "move" and p.sz == "w" and p.operands[0].mode == "pcindex"
                        and p.operands[0].target == base and p.operands[1].mode == "dreg"):
                    return base in self.tables or self.offset_table(base, base, ins.addr)
        elif dst.mode == "index":
            # lea (tbl).l,aN ... jmp (d,aN,Xn)
            for p in reversed(prev):
                if p.op == "lea" and p.operands[1].reg == dst.reg and p.operands[0].mode in ("absl", "pcdisp"):
                    base = p.operands[0].target + dst.disp
                    break
                if any(o.mode == "areg" and o.reg == dst.reg for o in p.operands[1:]):
                    break
        if base is None:
            return False
        if base in self.tables:
            return True
        start = base
        while ins.addr <= start < end:   # table begins inside/at the jump itself
            start += 2
        for skip in (0, 2, 4, 6):
            if self.branch_table(start + skip, ins.addr, table_addr=base):
                return True
        if start == end:
            # computed entry into the code that follows (e.g. a run of andi.w)
            self.add_entry(start, ins.addr)
            return True
        return False

    BRANCH_LIKE = ("bra", "bsr", "rts", "jmp", "nop")

    def branch_table(self, base, src, count=None, table_addr=None):
        """Run of branch-like instructions (bra/bsr/jmp abs/rts/nop), each one a
        possible jump table entry."""
        entries = []
        a = base
        while count is None or len(entries) < count:
            try:
                i = self.dec.decode(a)
            except m68k.Invalid:
                break
            if i.op not in self.BRANCH_LIKE:
                break
            if i.op == "jmp" and i.operands[0].mode not in ("absl", "absw"):
                break
            if i.op == "nop" and not entries:
                break
            if i.op != "nop":
                entries.append(a)
            a += i.size
        if not entries:
            return False
        key = table_addr if table_addr is not None else base
        self.tables[key] = ("bra", entries, a - base, base)
        self.names.setdefault(key, "jtbl_%06X" % key)
        for e in entries:
            self.add_entry(e, src)
        return True

    def offset_table(self, addr, base, src, count=None, code=True):
        entries = []
        a = addr
        limit = len(self.rom)
        while (count is None and a < limit) or (count is not None and len(entries) < count):
            off = m68k.sx16(self.dec.w(a))
            t = base + off
            if count is None:
                if off & 1 or not self.in_rom(t) or t <= a and t >= addr:
                    break
                if a != addr and (a in self.labels):
                    break
            entries.append(t)
            if t > a:
                limit = min(limit, t)
            a += 2
        if not entries:
            return False
        self.tables[addr] = ("w", entries, a - addr, base)
        self.names.setdefault(addr, "otbl_%06X" % addr)
        for t in entries:
            if code:
                self.add_entry(t, src)
            else:
                self.labels.add(t)
        return True

    def table(self, s):
        cnt = int(s.args["count"]) if "count" in s.args else None
        code = s.args.get("code", "1") != "0"
        if s.typ == "jt_bra":
            self.branch_table(s.addr, "symbols", cnt)
        elif s.typ == "jt_w":
            base = int(s.args["base"], 16) if "base" in s.args else s.addr
            self.offset_table(s.addr, base, "symbols", cnt, code)
        elif s.typ == "ptr_l":
            entries = []
            for k in range(cnt):
                entries.append(int.from_bytes(self.rom[s.addr + 4 * k:s.addr + 4 * k + 4], "big"))
            self.tables[s.addr] = ("l", entries, 4 * cnt, None)
            for t in entries:
                if code:
                    self.add_entry(t, "symbols")
                else:
                    self.labels.add(t)

    # ------------------------------------------------------------------
    def code_bytes(self):
        return sum(i.size for i in self.code.values())

    def report(self, out=sys.stdout):
        n = len(self.rom)
        cb = self.code_bytes()
        out.write("code: %d instructions, %d bytes (%.1f%% of ROM)\n" % (
            len(self.code), cb, 100.0 * cb / n))
        out.write("tables: %d\n" % len(self.tables))
        ok = {sym.addr for sym in self.symbols if sym.typ == "resolved"}
        unresolved = [a for a in self.unresolved if a not in ok]
        out.write("unresolved indirect jumps: %d (%d marked resolved in symbols.txt)\n" % (
            len(unresolved), len(self.unresolved) - len(unresolved)))
        for a in unresolved:
            out.write("  $%06x %s\n" % (a, self.code[a].render()))
        out.write("errors: %d\n" % len(self.errors))
        for a, msg in self.errors[:200]:
            out.write("  $%06x %s\n" % (a, msg))

    # ------------------------------------------------------------------
    def static_pointer_tables(self):
        """Tables of 32-bit pointers read by code through an index:
        move.l/movea.l (tbl,pc,Xn) or lea (tbl).l,aN ... move.l (d,aN,Xn).
        Returns {addr: value}. Entries are accepted while they look like
        ROM pointers (flag byte $00/$80) and stop at the next label."""
        notptr = set()
        for sym in self.symbols:
            if sym.typ == "notptr":
                notptr.update(range(sym.addr, sym.addr + int(sym.args.get("len", "1"), 16)))
        bases = set()
        code = self.code
        addrs = sorted(code)
        for k, a in enumerate(addrs):
            ins = code[a]
            if ins.op != "movea" or ins.sz != "l":
                continue
            src = ins.operands[0]
            if src.mode == "pcindex":
                bases.add(src.target)
            elif src.mode == "index":
                # look back a few instructions for lea (tbl).l,aN / lea (tbl,pc),aN
                for j in range(k - 1, max(0, k - 10), -1):
                    p = code[addrs[j]]
                    if (p.op == "lea" and p.operands[1].reg == src.reg
                            and p.operands[0].mode in ("absl", "pcdisp")):
                        bases.add(p.operands[0].target + src.disp)
                        break
                    if any(o.mode == "areg" and o.reg == src.reg for o in p.operands[1:]):
                        break
        out = {}
        n = len(self.rom)
        for b in sorted(bases):
            a = b
            while 0x200 <= a <= n - 4:
                if a != b and (a in self.labels or a in code or a in self.code_owner):
                    break
                v = int.from_bytes(self.rom[a:a + 4], "big")
                lo = v & 0xFFFFFF
                if (v >> 24) not in (0, 0x80) or not 0x200 <= lo < n or a in notptr:
                    break
                out[a] = v
                a += 4
        return out
