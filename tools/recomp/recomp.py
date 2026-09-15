#!/usr/bin/env python3
"""
Static recompiler: 68000 game code -> C.

Works from an assembled image and its vasm listing (-L), so the original ROM
and patched builds (make patched) are handled the same way: every listing line
whose source is an instruction is decoded from the image and translated.

Output: one C function per basic block, returning the address of the next
block; a lookup table maps 68000 addresses to blocks. Blocks start at branch
and call targets, return addresses, labels, vector targets and wait loops
(`waitloop` entries in symbols.txt, and loops that only test a memory location).

usage: recomp.py IMAGE LISTING OUT.c [--symbols symbols.txt] [--code-map MAP.c]

--code-map also writes the block structure without any code: for every block
its address, its number of instructions and how it ends, and the wait loop
addresses. The runtime translator (port/src/rt_translate.c) decodes the
instructions from the user's ROM with it and runs them as these blocks.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "disasm"))
import m68k  # noqa: E402
import m68k_c  # noqa: E402
import analyze  # noqa: E402

DIRECTIVES = {"dc.b", "dc.w", "dc.l", "dcb.b", "dcb.w", "dcb.l", "incbin", "equ", "ifne", "ifgt",
              "fail", "endif", "org", "even", "cnop", "rorg", "="}
CODE_LIMIT = 0x80000


def parse_listing(path):
    """Returns (instruction addresses, label addresses, nop padding starts)."""
    insns, labels, pads = [], set(), []
    pending_label = False
    line_re = re.compile(r"^00:([0-9A-F]{8}) ([0-9A-F]+)\s+\d+: (.*)$")
    src_re = re.compile(r"^\s+\d+: (.*)$")
    for line in open(path, errors="replace"):
        if line.startswith("Symbols by name:"):
            break
        m = line_re.match(line.rstrip("\n"))
        if m:
            addr = int(m.group(1), 16)
            text = m.group(3).split(";", 1)[0].strip()
            if pending_label:
                labels.add(addr)
                pending_label = False
            tok = text.split()
            if not tok:
                continue
            first = tok[0].lower()
            if first.endswith(":"):          # label on the same line
                labels.add(addr)
                tok = tok[1:]
                if not tok:
                    continue
                first = tok[0].lower()
            if first == "dcb.w" and text.rstrip().lower().endswith(",$4e71"):
                pads.append(addr)            # overlay padding: nop instructions
                continue
            if first in DIRECTIVES or (len(tok) > 1 and tok[1].lower() in ("equ", "=")):
                continue
            insns.append(addr)
            continue
        m = src_re.match(line.rstrip("\n"))
        if m:
            text = m.group(1).split(";", 1)[0].rstrip()
            if re.match(r"^[A-Za-z_.][\w.]*:\s*$", text):
                pending_label = True
    return insns, labels, pads


def cycle_cost(ins):
    """Rough 68000 cycle cost, only used as a hang guard."""
    base = {"divs": 158, "divu": 140, "muls": 70, "mulu": 70, "movem": 60}.get(ins.op, 10)
    return base + 4 * (ins.size - 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("listing")
    ap.add_argument("out")
    ap.add_argument("--symbols", default=os.path.join(ROOT, "symbols.txt"))
    ap.add_argument("--code-map")
    a = ap.parse_args()

    img = open(a.image, "rb").read()
    dec = m68k.Decoder(img)
    addrs, labels, pads = parse_listing(a.listing)
    known = set(addrs)
    for p in pads:
        while p not in known and int.from_bytes(img[p:p + 2], "big") == 0x4E71:
            addrs.append(p)
            known.add(p)
            p += 2
    code = {}
    for addr in addrs:
        if addr >= CODE_LIMIT:
            continue
        try:
            code[addr] = dec.decode(addr)
        except m68k.Invalid as e:
            print("warning: listing instruction at $%06x does not decode (%s)" % (addr, e))
    syms = analyze.load_symbols(a.symbols)
    waits = {s.addr for s in syms if s.typ == "waitloop"}
    # busy-wait loops of overlay code: "test a memory location, branch back to
    # the test" (the loop can only end when an interrupt changes the memory)
    for ins in code.values():
        if ins.flow == "cbranch" and len(ins.targets) == 1:
            t = ins.targets[0]
            test = code.get(t)
            if (test is not None and t + test.size == ins.addr and test.op in ("cmpi", "tst", "btst")
                    and test.operands and test.operands[-1].mode in ("absw", "absl")):
                waits.add(t)

    starts = set(waits)
    for i in range(64):
        v = int.from_bytes(img[i * 4:i * 4 + 4], "big")
        if v in code:
            starts.add(v)
    starts.update(l for l in labels if l in code)
    for ins in code.values():
        starts.update(t for t in ins.targets if t in code)
        if ins.op in ("jsr", "bsr", "trap", "chk", "divs", "divu", "trapv"):
            starts.add(ins.addr + ins.size)        # return address
        if ins.flow in (m68k.BRANCH, m68k.JUMP, m68k.IJUMP, m68k.RETURN, m68k.STOP):
            starts.add(ins.addr + ins.size)        # code after an unconditional transfer
    starts = {s for s in starts if s in code}

    out = ['/* Generated by tools/recomp/recomp.py from %s - do not edit */' % os.path.basename(a.image),
           '#include "m68k_rt.h"', '#include "recomp_rt.h"', "",
           "#define GOTO(e) return (e)", ""]
    order = sorted(code)
    blocks = []
    code_map = []                                  # (address, instructions, end)
    unsupported = 0
    for addr in order:
        if addr not in starts:
            continue
        name = "b_%06x" % addr
        blocks.append((addr, name))
        body = ["static uint32_t %s(M68K *c)" % name, "{"]
        p = addr
        cost = 0
        count, end = 0, 0
        while True:
            ins = code.get(p)
            if ins is None:
                body.append("  return rt_bad_pc(c, 0x%06x);" % p)
                end = 1
                break
            count += 1
            cost += cycle_cost(ins)
            body.append("  /* $%06x %s */" % (p, ins.render().replace("\t", " ").replace("*/", "* /")))
            body.append("  c->pc = 0x%06x; RT_HOOK(c, 0x%06x);" % (p, p))
            try:
                lines = m68k_c.translate(ins)
            except m68k_c.Unsupported as e:
                unsupported += 1
                body.append("  return rt_unsupported(c, 0x%06x); /* %s */" % (p, e))
                end = 2
                break
            nxt = p + ins.size
            # a backward branch into a wait loop means the loop repeats: let a
            # video frame pass first (the first pass through the loop does not wait)
            for t in ins.targets:
                if t in waits and t <= p:
                    g = "GOTO(0x%08xu);" % t
                    lines = [ln.replace(g, "{ rt_wait_point(c, 0x%06x); %s }" % (t, g)) for ln in lines]
            body.append("  {")
            for ln in lines:
                body.append("    " + ln.replace("NEXT();", "goto n_%06x;" % nxt if nxt not in starts else
                                                "return 0x%06x;" % nxt))
            body.append("  }")
            if ins.flow in (m68k.BRANCH, m68k.JUMP, m68k.IJUMP, m68k.RETURN, m68k.STOP) or ins.op in ("jsr", "bsr"):
                break
            if nxt in starts:
                break
            body.append("  n_%06x:;" % nxt)
            p = nxt
        body.insert(2, "  rt_charge(%d);" % cost)
        code_map.append((addr, count, end))
        body.append("}")
        out.extend(body)
        out.append("")
    out.append("const RtBlock rt_blocks[] = {")
    out.extend("  {0x%06x, %s}," % (addr, name) for addr, name in blocks)
    out.append("};")
    out.append("const int rt_block_count = %d;" % len(blocks))
    open(a.out, "w").write("\n".join(out) + "\n")
    if a.code_map:
        m = ["/* Generated by tools/recomp/recomp.py (--code-map) - do not edit */",
             "/* block structure of the game code: addresses and counts only */",
             '#include "rt_translate.h"', "",
             "const RtCodeBlock rt_code_map[] = {"]
        m.extend("  {0x%06x, %d, %d}," % b for b in code_map)
        m.append("};")
        m.append("const int rt_code_map_count = %d;" % len(code_map))
        m.append("")
        wl = sorted(waits & set(code))
        m.append("const uint32_t rt_code_waits[] = {")
        m.extend("  0x%06x," % w for w in wl)
        m.append("};")
        m.append("const int rt_code_wait_count = %d;" % len(wl))
        open(a.code_map, "w").write("\n".join(m) + "\n")
    print("%d instructions, %d blocks, %d unsupported, %d wait points" % (
        len(code), len(blocks), unsupported, len(waits & set(code))))


if __name__ == "__main__":
    main()
