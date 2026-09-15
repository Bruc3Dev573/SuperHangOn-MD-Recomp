#!/usr/bin/env python3
"""
Generate the reassemblable vasm source for the ROM.

usage: gen.py ROM SYMBOLS TRACE_ENTRIES OUT.asm [--report FILE]

Everything that is not proven code is emitted as data; data beyond the code
area is included from the base ROM with incbin (the ROM is not part of the
repository).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import m68k  # noqa: E402
import analyze  # noqa: E402

BASEROM_PATH = "rom/baserom.md"
IMM_PTR_MIN = 0x4512      # long immediates at or above this (and not round) are addresses
INCBIN_MIN = 256          # data runs at least this long go through incbin
DATA_PER_LINE = 16


def load_trace_entries(path):
    out = []
    if path and os.path.exists(path):
        for line in open(path):
            line = line.split("#", 1)[0].strip()
            if line:
                out.append(int(line, 16))
    return out


def load_overlays(dirs):
    """Hand-written patches: every .asm file in the given directories starts
    with one directive line:
        ; replace $START-$END   the file replaces original bytes [START,END) and is
                                padded with nops to the same size, so no address
                                moves (add "grow" to allow a different size)
        ; insert $ADDR          the file is inserted before original address ADDR
    A file in a later directory replaces the file with the same name in an
    earlier one (variants of an overlay set).
    Returns (replace {start: (end, text, path, grow)}, insert {addr: [(text, path)]})."""
    replace, insert = {}, {}
    # later directories override files with the same name in earlier ones
    chosen = {}
    for d in dirs:
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".asm"):
                chosen[fn] = os.path.join(d, fn)
    for fn in sorted(chosen, key=lambda f: (dirs.index(os.path.dirname(chosen[f])), f)):
        path = chosen[fn]
        text = open(path).read()
        first = text.lstrip().split("\n", 1)[0].split()
        if first[:2] == [";", "replace"]:
            a, b = first[2].replace("$", "").split("-")
            a, b = int(a, 16), int(b, 16)
            for s0, (e0, _, p0, _) in replace.items():
                if a < e0 and s0 < b:
                    raise SystemExit("overlay %s overlaps %s" % (path, p0))
            grow = len(first) > 3 and first[3] == "grow"
            replace[a] = (b, text, path, grow)
        elif first[:2] == [";", "insert"]:
            insert.setdefault(int(first[2].replace("$", ""), 16), []).append((text, path))
        else:
            raise SystemExit("%s: first line must be '; replace $START-$END' or '; insert $ADDR'" % path)
    return replace, insert


def load_pointers(path):
    out = {}
    if path and os.path.exists(path):
        for line in open(path):
            line = line.split("#", 1)[0].split()
            if line:
                out[int(line[0], 16)] = int(line[1], 16)
    return out


class Generator:
    def __init__(self, rom, an, rom_end, pointers=None, shifts=None, overlays=None):
        self.rom = rom
        self.an = an
        self.rom_end = rom_end
        merged = an.static_pointer_tables()
        merged.update(pointers or {})
        self.pointers = {a: v for a, v in merged.items()
                         if a not in an.code_owner and a >= 0x200}
        self.shifts = shifts or {}      # addr -> bytes inserted (>0) or filler bytes dropped (<0)
        self.pins = {sym.addr for sym in an.symbols if sym.typ == "pin"}
        # $FF runs that absorb size changes of the code area: emitted as padding
        # up to their original end address, so everything after stays in place
        self.fillers = {sym.addr: int(sym.args["end"], 16)
                        for sym in an.symbols if sym.typ == "filler"}
        self.replace, self.insert = overlays or ({}, {})
        # instructions whose 32-bit immediate is a ROM address
        self.imm_ptrs = {}
        forced = {sym.addr for sym in an.symbols if sym.typ == "immptr"}
        denied = {sym.addr for sym in an.symbols if sym.typ == "notimmptr"}
        for ins in an.code.values():
            for o in ins.operands:
                if (o.mode == "imm" and o.size == "l" and o.value < rom_end
                        and ins.op in ("move", "movea", "cmpi", "cmpa", "cmp")):
                    v = o.value
                    auto = IMM_PTR_MIN <= v and not v & 1 and v & 0xFFF
                    if (auto or ins.addr in forced) and ins.addr not in denied:
                        self.imm_ptrs[ins.addr] = v
        self.calls = set()
        for ins in an.code.values():
            if ins.flow == m68k.CALL:
                self.calls.update(ins.targets)
        self.ram_blocks = {rom_addr: (vma, ln) for rom_addr, vma, ln in an.ram_blocks}

    # --------------------------------------------------------------- labels
    def name_for(self, a):
        if a in self.an.names:
            return self.an.names[a]
        if a in self.an.code:
            return ("sub_%06X" if a in self.calls else "loc_%06X") % a
        return "dat_%06X" % a

    def build_labels(self):
        an = self.an
        wanted = set(an.labels)
        wanted.update(an.names)
        wanted.update(v & 0xFFFFFF for v in self.pointers.values())
        wanted.update(self.shifts)
        wanted.update(self.imm_ptrs.values())
        wanted.update(self.pins)
        wanted.update(self.fillers)
        wanted.update(self.insert)
        for start, (end, _, _, _) in self.replace.items():
            wanted.update((start, end))
        for kind, entries, size, base in an.tables.values():
            if kind in ("w", "l"):
                wanted.update(t for t in entries if 0 <= t < self.rom_end)
        # item boundaries: every code instruction, else data (byte granularity)
        self.wanted = {a for a in wanted if 0 <= a < self.rom_end}
        # a label inside an instruction is emitted as owner+offset
        for a in list(self.wanted):
            o = an.code_owner.get(a)
            if o is not None and o != a:
                self.wanted.add(o)

    def label(self, a):
        """Symbol for an address referenced from code, or None."""
        if not (0 <= a < self.rom_end):
            return None
        if a == self.rom_end:
            return "RomEnd"
        if a in self.wanted:
            return self.name_for(a)
        return None

    # --------------------------------------------------------------- output
    def run(self, out):
        an = self.an
        rom = self.rom
        self.build_labels()
        w = out.write
        w("; Super Hang-On (Japan, USA) (En,Ja) - Mega Drive\n")
        w("; Generated by tools/disasm/gen.py - reassemble with vasmm68k_mot -no-opt\n\n")
        w("\torg\t0\n\n")
        # RAM block equates
        for rom_addr, (vma, ln) in sorted(self.ram_blocks.items()):
            w("%s_RAM\tequ\t$%x\n" % (self.name_for(rom_addr), vma))
        w("\n")

        # relocation tests keep the image size constant by eating into the
        # trailing $FF padding, so boot-time checksum timing does not change
        end = self.rom_end
        total_shift = sum(self.shifts.values())
        if total_shift < 0:
            raise SystemExit("net negative shift")
        if total_shift:
            if any(b != 0xFF for b in rom[end - total_shift:end]):
                raise SystemExit("not enough trailing padding for shift")
            end -= total_shift
        a = 0
        while a < end:
            if a in self.shifts and self.shifts[a] > 0:
                w("\tdcb.b\t$%x,$ff\t; SHIFT TEST PADDING\n" % self.shifts[a])
            elif a in self.shifts and self.shifts[a] < 0:
                n = -self.shifts[a]
                if any(b != rom[a] for b in rom[a:a + n]) or any(x in self.wanted for x in range(a + 1, a + n)):
                    raise SystemExit("cannot drop $%x bytes at $%x: not filler" % (n, a))
                w("\t; SHIFT TEST: dropped $%x filler bytes\n" % n)
                a += n
                continue
            for text, path in self.insert.get(a, []):
                w("\n; ---- overlay %s ----\n%s\n; ---- end of overlay ----\n" % (path, text))
            if a in self.replace:
                end_r, text, path, grow = self.replace[a]
                w("\n; ---- overlay %s (replaces $%x-$%x) ----\n__ovl_%x:\n%s\n" % (path, a, end_r, a, text))
                if not grow:
                    n = end_r - a
                    w("\tifgt\t*-__ovl_%x-$%x\n\tfail\t\"overlay %s is larger than $%x bytes\"\n\tendif\n"
                      % (a, n, path, n))
                    w("\tdcb.w\t($%x-(*-__ovl_%x))/2,$4e71\t; pad to the original size\n" % (n, a))
                w("; ---- end of overlay ----\n")
                a = end_r
                continue
            if a in self.pins:
                w("\tifne\t*-$%x\n\tfail\t\"address $%x is fixed (see symbols.txt pin)\"\n\tendif\n" % (a, a))
            if a in self.fillers and not self.shifts:
                fend = self.fillers[a]
                if any(b != 0xFF for b in rom[a:fend]) or any(x in self.wanted for x in range(a + 1, fend)):
                    raise SystemExit("filler $%x-$%x is not free $FF space" % (a, fend))
                self.emit_label(w, a)
                w("\tifgt\t*-$%x\n\tfail\t\"code area grew past the filler ending at $%x\"\n\tendif\n" % (fend, fend))
                w("\tdcb.b\t$%x-*,$ff\t; free space\n" % fend)
                a = fend
                continue
            if a in self.pointers:
                self.emit_label(w, a)
                v = self.pointers[a]
                lab = self.opnd_label(v & 0xFFFFFF) or "$%x" % (v & 0xFFFFFF)
                w("\tdc.l\t%s\n" % (lab if v >> 24 == 0 else "$%02x000000|%s" % (v >> 24, lab)))
                for k in range(1, 4):
                    if a + k in self.wanted:
                        w("%s\tequ\t*-%d\n" % (self.name_for(a + k), 4 - k))
                a += 4
                continue
            if a in self.ram_blocks:
                a = self.emit_ram_block(w, a)
                continue
            if a in an.code:
                ins = an.code[a]
                self.emit_label(w, a)
                w("\t%s\n" % ins.render(self.opnd_label,
                                         self.opnd_label if a in self.imm_ptrs else None))
                for k in range(1, ins.size):
                    if a + k in self.wanted:
                        w("%s\tequ\t%s+%d\n" % (self.name_for(a + k), self.name_for(a), k))
                a += ins.size
                continue
            if a in an.tables and an.tables[a][0] in ("w", "l"):
                a = self.emit_table(w, a)
                continue
            if a < 0x100:
                a = self.emit_vectors(w)
                continue
            if a < 0x200:
                a = self.emit_header(w)
                continue
            a = self.emit_data(w, a, end)
        w("RomEnd:\n")

    def opnd_label(self, t):
        if t == self.rom_end:
            return "RomEnd"
        for rom_addr, (vma, ln) in self.ram_blocks.items():
            if vma <= t < vma + ln:
                off = t - vma
                base = "%s_RAM" % self.name_for(rom_addr)
                return base if off == 0 else "%s+$%x" % (base, off)
        return self.label(t)

    def emit_label(self, w, a):
        if a in self.wanted:
            if a in self.an.names or a in self.calls:
                w("\n")
            w("%s:\n" % self.name_for(a))

    def emit_inner_equs(self, w, base_name, start, end):
        for p in range(start, end):
            if p in self.wanted and p != 0 and p != 0x100:
                w("%s\tequ\t%s+$%x\n" % (self.name_for(p), base_name, p - start))

    def next_boundary(self, a):
        """First address > a that starts code, a table, a RAM block, or a label."""
        b = a + 1
        while b < self.rom_end:
            if (b in self.an.code or b in self.an.tables or b in self.ram_blocks or b in self.wanted
                    or b in self.pointers or b in self.shifts or b in self.replace):
                return b
            b += 1
        return self.rom_end

    def emit_data(self, w, a, limit=None):
        self.emit_label(w, a)
        end = min(self.next_boundary(a), limit or self.rom_end)
        rom = self.rom
        ln = end - a
        if ln >= INCBIN_MIN:
            w("\tincbin\t\"%s\",$%x,$%x\n" % (BASEROM_PATH, a, ln))
        else:
            for p in range(a, end, DATA_PER_LINE):
                chunk = rom[p:min(end, p + DATA_PER_LINE)]
                w("\tdc.b\t%s\n" % ",".join("$%02x" % b for b in chunk))
        return end

    def emit_table(self, w, a):
        kind, entries, size, base = self.an.tables[a]
        self.emit_label(w, a)
        if kind == "w":
            bl = self.label(base) or "$%x" % base
            for t in entries:
                tl = self.label(t)
                w("\tdc.w\t%s-%s\n" % (tl, bl))
        else:
            for t in entries:
                tl = self.label(t) or "$%x" % t
                w("\tdc.l\t%s\n" % tl)
        return a + size

    def emit_ram_block(self, w, a):
        vma, ln = self.ram_blocks[a]
        name = self.name_for(a)
        w("\n; ---- code copied to RAM at $%x and executed there ----\n" % vma)
        w("%s:\n" % name)
        dec = m68k.Decoder(self.rom[a:a + ln], base=vma)
        items = []
        p = vma
        while p < vma + ln:
            try:
                ins = dec.decode(p)
            except m68k.Invalid:
                ins = None
            items.append((p, ins))
            p += ins.size if ins else 2
        targets = {t for _, ins in items if ins for t in ins.targets}

        def local(t):
            return "%s_%x" % (name, t - vma)

        for p, ins in items:
            if p != vma and (p in targets or (p - vma + a) in self.wanted):
                w("%s:\n" % local(p))
            if ins is None:
                w("\tdc.w\t$%04x\n" % dec.w(p))
                continue

            def lab(t, ins=ins):
                if vma <= t < vma + ln:
                    if t in ins.targets:
                        return local(t) if t != vma else name
                    off = t - vma
                    return "%s_RAM+$%x" % (name, off) if off else "%s_RAM" % name
                return self.opnd_label(t)
            w("\t%s\n" % ins.render(lab))
        w("%s_End:\n" % name)
        return a + ln

    def emit_vectors(self, w):
        rom = self.rom
        w("Vectors:\n")
        for i in range(64):
            v = int.from_bytes(rom[i * 4:i * 4 + 4], "big")
            lab = self.opnd_label(v) if v else None
            w("\tdc.l\t%s\n" % (lab if lab else "$%08x" % v))
        self.emit_inner_equs(w, "Vectors", 0, 0x100)
        return 0x100

    def emit_header(self, w):
        rom = self.rom
        w("\nHeader:\n")
        fields = [(0x100, 0x110, "system"), (0x110, 0x120, "copyright"),
                  (0x120, 0x150, "domestic name"), (0x150, 0x180, "overseas name"),
                  (0x180, 0x18e, "serial"), (0x18e, 0x190, "checksum"),
                  (0x190, 0x1a0, "I/O support"), (0x1a0, 0x1a8, "ROM range"),
                  (0x1a8, 0x1b0, "RAM range"), (0x1b0, 0x1bc, "SRAM"),
                  (0x1bc, 0x1c8, "modem"), (0x1c8, 0x1f0, "memo"),
                  (0x1f0, 0x200, "region")]
        for s, e, what in fields:
            data = rom[s:e]
            if what == "checksum":
                w("\tdc.w\t$%04x\t\t; %s\n" % (int.from_bytes(data, "big"), what))
            elif what == "ROM range":
                start = int.from_bytes(data[:4], "big")
                end = int.from_bytes(data[4:], "big")
                w("\tdc.l\t$%08x,%s\t; %s\n" % (
                    start, "RomEnd-1" if end == self.rom_end - 1 else "$%08x" % end, what))
            elif what == "RAM range":
                w("\tdc.l\t$%08x,$%08x\t; %s\n" % (int.from_bytes(data[:4], "big"),
                                                   int.from_bytes(data[4:], "big"), what))
            elif all(32 <= b < 127 and b != 34 for b in data):
                w("\tdc.b\t\"%s\"\t; %s\n" % (data.decode("ascii"), what))
            else:
                w("\tdc.b\t%s\t; %s\n" % (",".join("$%02x" % b for b in data), what))
        self.emit_inner_equs(w, "Header", 0x100, 0x200)
        return 0x200


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("symbols")
    ap.add_argument("trace")
    ap.add_argument("out")
    ap.add_argument("--report")
    ap.add_argument("--pointers", default="analysis/pointers.txt")
    ap.add_argument("--shift", action="append", default=[],
                    help="ADDR:COUNT insert COUNT padding bytes before ADDR, or drop -COUNT "
                         "filler bytes at ADDR (relocation test)")
    ap.add_argument("--overlay", action="append", default=[],
                    help="directory of hand-written patch .asm files (see load_overlays)")
    args = ap.parse_args()
    shifts = {}
    for sh in args.shift:
        addr, cnt = sh.split(":")
        shifts[int(addr, 16)] = int(cnt, 0)
    rom = open(args.rom, "rb").read()
    syms = analyze.load_symbols(args.symbols)
    an = analyze.Analysis(rom, syms, load_trace_entries(args.trace))
    an.run()
    if args.report:
        with open(args.report, "w") as f:
            an.report(f)
    with open(args.out, "w") as f:
        Generator(rom, an, len(rom), load_pointers(args.pointers), shifts,
                  load_overlays(args.overlay)).run(f)


if __name__ == "__main__":
    main()
