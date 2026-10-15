#!/usr/bin/env python3
"""Lists the code the game runs from work RAM, from a WRAM snapshot.

The game unpacks code into $7E:2000 and swaps it between the menus and races, so take
a snapshot while the code you want is loaded (tg_headless --dump-wram) and list it:

    python3 tools/disasm_ram.py --wram race.bin --coverage disasm/coverage.tgcov \
        --start 7E2000 --end 7E3600 > race_overlay.lst

Only instructions recorded as executed with exactly these opcode bytes are decoded.
The listing is derived from your ROM; keep it local.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from disasm import OPS, insn_len, load_ram_names, ram_name, HW_REGS  # noqa: E402
from tgcov import Coverage  # noqa: E402

M16X16, M16X8, M8X16, M8X8 = 1, 2, 4, 8  # mode bits as stored in ram_ops (flags >> 3)


def fmt(mem, base, a, n, m8, x8, names):
    i = a - base
    op = mem[i]
    mnem, mode = OPS[op]
    v8 = mem[i + 1] if n > 1 else 0
    v16 = v8 | (mem[i + 2] << 8 if n > 2 else 0)
    v24 = v16 | (mem[i + 3] << 16 if n > 3 else 0)
    pc = a & 0xFFFF
    note = None
    if mode in ("imp",):
        t = mnem
    elif mode == "acc":
        t = f"{mnem} a"
    elif mode in ("immm", "immx"):
        t = f"{mnem} #${v8:02X}" if n == 2 else f"{mnem} #${v16:04X}"
    elif mode == "imm8":
        t = f"{mnem} #${v8:02X}"
    elif mode == "rel":
        t = f"{mnem} ${(pc + 2 + (v8 - 256 if v8 >= 128 else v8)) & 0xFFFF:04X}"
    elif mode == "rell":
        t = f"{mnem} ${(pc + 3 + (v16 - 65536 if v16 >= 32768 else v16)) & 0xFFFF:04X}"
    elif mode == "blk":
        t = f"{mnem} #${mem[i + 2]:02X},#${v8:02X}"
    elif mode == "pea":
        t = f"pea ${v16:04X}"
    elif mode in ("long", "longx"):
        t = f"{mnem} ${v24:06X}" + (",x" if mode == "longx" else "")
        if (v24 >> 16) in (0x7E, 0x7F):
            note = ram_name(names, v24)
        elif (v24 & 0xFFFF) in HW_REGS and (v24 >> 16) & 0x40 == 0:
            note = HW_REGS[v24 & 0xFFFF]
    elif mode.startswith("abs"):
        suffix = {"abs": "", "absx": ",x", "absy": ",y"}.get(mode)
        if suffix is None:
            t = {"absi": f"{mnem} (${v16:04X})", "absix": f"{mnem} (${v16:04X},x)", "absil": f"{mnem} [${v16:04X}]"}[mode]
        else:
            t = f"{mnem} ${v16:04X}{suffix}"
            note = HW_REGS.get(v16) or (ram_name(names, 0x7E0000 | v16) if v16 < 0x2000 else None)
    else:
        form = {"dp": "${:02X}", "dpx": "${:02X},x", "dpy": "${:02X},y", "dpi": "(${:02X})",
                "dpix": "(${:02X},x)", "dpiy": "(${:02X}),y", "dpil": "[${:02X}]", "dpily": "[${:02X}],y",
                "sr": "${:02X},s", "sriy": "(${:02X},s),y"}[mode]
        t = f"{mnem} " + form.format(v8)
        if not mode.startswith("sr"):
            note = ram_name(names, 0x7E0000 | v8)
    return t, note


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wram", required=True)
    ap.add_argument("--coverage", required=True)
    ap.add_argument("--ram", default=os.path.join(HERE, "..", "disasm", "ram.txt"))
    ap.add_argument("--start", default="7E2000")
    ap.add_argument("--end", default="7E3600")
    args = ap.parse_args()
    wram = open(args.wram, "rb").read()
    cov = Coverage.load(args.coverage)
    names = load_ram_names(args.ram)
    start, end = int(args.start, 16), int(args.end, 16)
    base = 0x7E0000
    seen = {}
    for addr, op, mode in cov.ram_ops:
        a = base + addr
        if start <= a < end and wram[addr] == op:
            seen.setdefault(a, set()).add(mode)
    a = start
    data = []

    def flush():
        if data:
            print(f"  .byte {','.join(f'${b:02X}' for b in data)}")
            data.clear()

    while a < end:
        modes = seen.get(a)
        if modes:
            m8 = all(md & (M8X16 | M8X8) for md in modes)
            x8 = all(md & (M16X8 | M8X8) for md in modes)
            lens = {insn_len(wram[a - base], bool(md & (M8X16 | M8X8)), bool(md & (M16X8 | M8X8))) for md in modes}
            if len(lens) == 1:
                n = lens.pop()
                flush()
                t, note = fmt(wram, base, a, n, m8, x8, names)
                print(f"  {t:<24}; {a >> 16:02X}:{a & 0xFFFF:04X}" + (f" {note}" if note else ""))
                a += n
                continue
        data.append(wram[a - base])
        if len(data) == 16:
            flush()
        a += 1
    flush()


if __name__ == "__main__":
    main()
