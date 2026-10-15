#!/usr/bin/env python3
"""Summarise a coverage file written by tg_headless --coverage."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tgcov import ROM, Coverage  # noqa: E402

OPCODE, OPERAND, READ = 1, 2, 4


def main():
    cov = Coverage.load(sys.argv[1])
    flags, entry, ram = cov.flags, cov.entry, cov.ram
    code = sum(1 for f in flags if f & (OPCODE | OPERAND))
    data = sum(1 for f in flags if f & READ and not f & (OPCODE | OPERAND))
    targets = sum(1 for e in entry if e & 1)
    calls = sum(1 for e in entry if e & 2)
    print(f"code bytes      {code:7d} ({100 * code / ROM:.1f}%)")
    print(f"data bytes read {data:7d} ({100 * data / ROM:.1f}%)")
    print(f"untouched       {ROM - code - data:7d} ({100 * (ROM - code - data) / ROM:.1f}%)")
    print(f"flow targets    {targets:7d}, call targets {calls}")
    ramop = [i for i, f in enumerate(ram) if f & OPCODE]
    print(f"RAM code addrs  {len(ramop)}, distinct RAM opcodes {len(cov.ram_ops)}")
    print("per bank (code/data/untouched %):")
    for b in range(ROM // 0x8000):
        s = flags[b * 0x8000:(b + 1) * 0x8000]
        c = sum(1 for f in s if f & 3)
        dd = sum(1 for f in s if f & 4 and not f & 3)
        print(f"  {b:02x}: {100*c/0x8000:5.1f} {100*dd/0x8000:5.1f} {100*(0x8000-c-dd)/0x8000:5.1f}")


if __name__ == "__main__":
    main()
