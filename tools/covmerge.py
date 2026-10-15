#!/usr/bin/env python3
"""Merges coverage files: covmerge.py OUT IN [IN ...] (OUT may also be an input)."""
import os
import sys

ROM = 0x80000
HEADER = b"TGCOV01\0"
SIZE = 8 + ROM * 5 + 0x20000


def main():
    out, ins = sys.argv[1], sys.argv[2:]
    acc = None
    for path in ins:
        if not os.path.exists(path):
            continue
        d = open(path, "rb").read()
        if d[:8] != HEADER or len(d) != SIZE:
            sys.exit(f"{path}: not a coverage file")
        if acc is None:
            acc = bytearray(d)
            continue
        old = bytes(acc[8:8 + ROM])
        o = 8
        for i in range(2 * ROM):  # flags and entry: union
            acc[o + i] |= d[o + i]
        o += 2 * ROM
        for i in range(ROM):  # data bank / direct page: keep the first seen
            if d[8 + i] & 1 and not old[i] & 1:
                acc[o + i] = d[o + i]
                acc[o + ROM + 2 * i:o + ROM + 2 * i + 2] = d[o + ROM + 2 * i:o + ROM + 2 * i + 2]
        o += 3 * ROM
        for i in range(0x20000):
            acc[o + i] |= d[o + i]
    if acc is None:
        sys.exit("no input files")
    open(out, "wb").write(acc)


if __name__ == "__main__":
    main()
