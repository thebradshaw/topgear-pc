#!/usr/bin/env python3
"""Summarise a coverage file written by tg_headless --coverage."""
import sys

ROM = 0x80000
OPCODE, OPERAND, READ = 1, 2, 4


def load(path):
    d = open(path, "rb").read()
    if d[:8] != b"TGCOV01\0":
        sys.exit(f"{path}: not a coverage file")
    o = 8
    flags = d[o:o + ROM]; o += ROM
    entry = d[o:o + ROM]; o += ROM
    db = d[o:o + ROM]; o += ROM
    dp = d[o:o + 2 * ROM]; o += 2 * ROM
    ram = d[o:o + 0x20000]
    return flags, entry, db, dp, ram


def main():
    flags, entry, _, _, ram = load(sys.argv[1])
    code = sum(1 for f in flags if f & (OPCODE | OPERAND))
    data = sum(1 for f in flags if f & READ and not f & (OPCODE | OPERAND))
    targets = sum(1 for e in entry if e & 1)
    calls = sum(1 for e in entry if e & 2)
    print(f"code bytes      {code:7d} ({100 * code / ROM:.1f}%)")
    print(f"data bytes read {data:7d} ({100 * data / ROM:.1f}%)")
    print(f"untouched       {ROM - code - data:7d} ({100 * (ROM - code - data) / ROM:.1f}%)")
    print(f"flow targets    {targets:7d}, call targets {calls}")
    ramop = [i for i, f in enumerate(ram) if f & OPCODE]
    print(f"RAM code addrs  {len(ramop)}: " + " ".join(f"{a:05x}" for a in ramop[:16]))
    print("per bank (code/data/untouched %):")
    for b in range(ROM // 0x8000):
        s = flags[b * 0x8000:(b + 1) * 0x8000]
        c = sum(1 for f in s if f & 3)
        dd = sum(1 for f in s if f & 4 and not f & 3)
        print(f"  {b:02x}: {100*c/0x8000:5.1f} {100*dd/0x8000:5.1f} {100*(0x8000-c-dd)/0x8000:5.1f}")


if __name__ == "__main__":
    main()
