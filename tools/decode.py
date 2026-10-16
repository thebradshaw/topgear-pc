#!/usr/bin/env python3
"""Decodes 65816 code straight from the ROM, for code no recording has run (the
disassembly leaves it as .byte lines).

  tools/decode.py --rom ROM 01:8318 [--end 01:8322 | --count N] [--m8] [--x8]

Register widths start as given (16-bit by default) and follow REP/SEP from there, so
check them against the code around the bytes.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import OPS, insn_len, rom_off  # noqa: E402


def parse_addr(s):
    bank, addr = s.split(":")
    return int(bank, 16), int(addr, 16)


def operand(mode, b, pc, bank):
    v8 = b[1] if len(b) > 1 else 0
    v16 = v8 | (b[2] << 8 if len(b) > 2 else 0)
    v24 = v16 | (b[3] << 16 if len(b) > 3 else 0)
    if mode in ("imp",):
        return ""
    if mode == "acc":
        return "a"
    if mode in ("imm8",):
        return f"#${v8:02X}"
    if mode in ("immm", "immx"):
        return f"#${v8:02X}" if len(b) == 2 else f"#${v16:04X}"
    if mode == "rel":
        d = v8 - 256 if v8 >= 128 else v8
        return f"${(pc + 2 + d) & 0xffff:04X}"
    if mode == "rell":
        d = v16 - 65536 if v16 >= 32768 else v16
        return f"${(pc + 3 + d) & 0xffff:04X}"
    fmt = {
        "dp": "z:${:02X}", "dpx": "z:${:02X},x", "dpy": "z:${:02X},y", "dpi": "(${:02X})",
        "dpix": "(${:02X},x)", "dpiy": "(${:02X}),y", "dpil": "[${:02X}]", "dpily": "[${:02X}],y",
        "sr": "${:02X},s", "sriy": "(${:02X},s),y",
    }
    if mode in fmt:
        return fmt[mode].format(v8)
    fmt16 = {"abs": "a:${:04X}", "absx": "a:${:04X},x", "absy": "a:${:04X},y", "absi": "(${:04X})",
             "absix": "(${:04X},x)", "absil": "[${:04X}]", "pea": "#${:04X}"}
    if mode in fmt16:
        return fmt16[mode].format(v16)
    if mode == "blk":
        return f"${b[2]:02X},${b[1]:02X}"
    if mode in ("long", "longx"):
        return f"${v24:06X}" + (",x" if mode == "longx" else "")
    return "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", required=True)
    ap.add_argument("start")
    ap.add_argument("--end")
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--m8", action="store_true")
    ap.add_argument("--x8", action="store_true")
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    bank, pc = parse_addr(args.start)
    end = parse_addr(args.end)[1] if args.end else None
    m8, x8 = args.m8, args.x8
    n = 0
    while (end is None and n < args.count) or (end is not None and pc < end):
        off = rom_off(bank, pc)
        op = rom[off]
        mnem, mode = OPS[op]
        length = insn_len(op, m8, x8)
        b = rom[off:off + length]
        text = f"{mnem} {operand(mode, b, pc, bank)}".strip()
        print(f"{bank:02X}:{pc:04X}  {' '.join(f'{x:02X}' for x in b):12s} {text}")
        if mnem == "rep":
            m8 = m8 and not b[1] & 0x20
            x8 = x8 and not b[1] & 0x10
        elif mnem == "sep":
            m8 = m8 or bool(b[1] & 0x20)
            x8 = x8 or bool(b[1] & 0x10)
        pc += length
        n += 1


if __name__ == "__main__":
    main()
