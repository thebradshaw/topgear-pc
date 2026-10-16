#!/usr/bin/env python3
"""Maps routines in a generated disassembly (tools/disasm.py output) for translation.

For each call target, follows every branch and jump from the entry point (not into
JSR/JSL calls) to find all the code the routine can run, then reports its size, the
routines it calls, whether it reaches undecoded bytes (code no recording has run), and
which translated routines (src/port/registry.c) are already done.

  tools/routines.py DISASM.s [--bank 00] [--leaves] [--name NAME]
"""
import argparse
import re
import sys
from pathlib import Path

INSN = re.compile(r'^\s+([a-z]{3})\b\s*([^;]*?)\s*;\s*([0-9A-F]{2}):([0-9A-F]{4})')
LABEL = re.compile(r'^([A-Za-z_]\w*):')
BRANCHES = {'bcc', 'bcs', 'beq', 'bne', 'bmi', 'bpl', 'bvc', 'bvs', 'bra', 'brl'}
ENDS = {'rts', 'rtl', 'rti', 'stp'}


def parse(path):
    insns = []  # (addr, mnemonic, operand)
    labels = {}  # name -> addr
    gaps = set()  # addresses of instructions followed by undecoded bytes
    pending = []
    for line in Path(path).read_text().splitlines():
        m = LABEL.match(line)
        if m:
            pending.append(m.group(1))
            continue
        m = INSN.match(line)
        if m:
            addr = int(m.group(3), 16) << 16 | int(m.group(4), 16)
            for name in pending:
                labels[name] = addr
            pending = []
            insns.append((addr, m.group(1), m.group(2).strip()))
            continue
        if '.byte' in line:
            if insns and not pending:
                gaps.add(insns[-1][0])
            pending = []
    return insns, labels, gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('disasm')
    ap.add_argument('--bank', default=None, help='only routines in this bank (hex)')
    ap.add_argument('--leaves', action='store_true', help='only routines that call nothing')
    ap.add_argument('--name', help='one routine, with its full instruction list')
    args = ap.parse_args()

    insns, labels, gaps = parse(args.disasm)
    index = {a: i for i, (a, _, _) in enumerate(insns)}
    names = {a: n for n, a in labels.items() if not n.startswith('L')}
    registry = Path(__file__).resolve().parent.parent / 'src/port/registry.c'
    done = set()
    if registry.exists():
        for m in re.finditer(r'\{\s*0x([0-9A-Fa-f]{6}),', registry.read_text()):
            done.add(int(m.group(1), 16))

    def target(op, bank):
        op = op.split(',')[0].strip()
        if op in labels:
            return labels[op]
        m = re.match(r'^\$([0-9A-F]{6})$', op)
        if m:
            return int(m.group(1), 16)
        m = re.match(r'^\$([0-9A-F]{4})$', op)
        if m:
            return bank << 16 | int(m.group(1), 16)
        return None

    entries = set()
    for a, mn, op in insns:
        if mn in ('jsr', 'jsl'):
            t = target(op, a >> 16)
            if t is not None:
                entries.add(t)
    entries |= {a for a in names if a in index}

    def extent(entry):
        # Also returns the instructions that fall through into undecoded bytes.
        seen, calls, falls, todo = set(), set(), set(), [entry]
        while todo:
            a = todo.pop()
            if a in seen or a not in index:
                continue
            i = index[a]
            while i < len(insns):
                addr, mn, op = insns[i]
                if addr in seen:
                    break
                seen.add(addr)
                bank = addr >> 16
                if mn in ('jsr', 'jsl'):
                    t = target(op, bank)
                    calls.add(t if t is not None else op)
                if mn in BRANCHES:
                    t = target(op, bank)
                    if t is not None:
                        todo.append(t)
                    if mn in ('bra', 'brl'):
                        break
                if mn in ('jmp', 'jml'):
                    t = target(op, bank)
                    if t is not None and not op.startswith('('):
                        todo.append(t)
                    break
                if mn in ENDS:
                    break
                if addr in gaps:
                    falls.add(addr)
                    break
                i += 1
        return seen, calls, falls

    def label_of(a):
        return names.get(a, f'${a >> 16:02X}:{a & 0xffff:04X}')

    # Undecoded blocks: labels followed by .byte; a routine branching to one has code no
    # recording has run.
    undecoded_addrs = set()
    lines = Path(args.disasm).read_text().splitlines()
    for i, line in enumerate(lines):
        m = LABEL.match(line)
        if m and i + 1 < len(lines) and '.byte' in lines[i + 1]:
            undecoded_addrs.add(m.group(1))

    rows = []
    for e in sorted(entries):
        if args.bank and f'{e >> 16:02X}' != args.bank.upper():
            continue
        if args.name and label_of(e) != args.name:
            continue
        seen, calls, falls = extent(e)
        refs = set(falls)
        for a in seen:
            mn, op = insns[index[a]][1], insns[index[a]][2]
            if mn in BRANCHES or mn in ('jmp',):
                op0 = op.split(',')[0].strip()
                if op0 in undecoded_addrs:
                    refs.add(op0)
        if args.leaves and calls:
            continue
        rows.append((len(seen), e, calls, refs))
        if args.name:
            for a in sorted(seen):
                print(f'{a >> 16:02X}:{a & 0xffff:04X} {insns[index[a]][1]} {insns[index[a]][2]}')
                if a in falls:
                    print('        (falls through into code no recording has run)')
    rows.sort()
    for n, e, calls, refs in rows:
        mark = 'done' if e in done else '    '
        c = ','.join(sorted(label_of(x) if isinstance(x, int) else x for x in calls))
        print(f'{e >> 16:02X}:{e & 0xffff:04X} {mark} {label_of(e):26s} {n:5d} insns'
              f'{"  unrun:" + str(len(refs)) if refs else ""}  calls: {c}')


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BrokenPipeError:
        pass
