#!/usr/bin/env python3
"""Recompute the Mega Drive header checksum (16-bit sum of big-endian words
from $200 to the end of the image) and store it at $18E.

usage: checksum.py ROM [--check]
"""
import sys


def checksum(data):
    s = 0
    for i in range(0x200, len(data) - 1, 2):
        s += (data[i] << 8) | data[i + 1]
    return s & 0xFFFF


def main():
    path = sys.argv[1]
    data = bytearray(open(path, "rb").read())
    cur = (data[0x18E] << 8) | data[0x18F]
    new = checksum(data)
    if "--check" in sys.argv:
        print("header $%04x computed $%04x %s" % (cur, new, "OK" if cur == new else "BAD"))
        sys.exit(0 if cur == new else 1)
    data[0x18E] = new >> 8
    data[0x18F] = new & 0xFF
    open(path, "wb").write(data)
    print("checksum $%04x -> $%04x" % (cur, new))


if __name__ == "__main__":
    main()
