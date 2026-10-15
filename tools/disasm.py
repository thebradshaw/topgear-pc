#!/usr/bin/env python3
"""Coverage-driven disassembler for Top Gear (USA).

Turns your ROM plus a coverage file (recorded by topgear / tg_headless) into ca65
source that reassembles to the identical ROM. Bytes the game was seen executing are
decoded as instructions with the register widths it actually used; everything else is
emitted as data. Names and comments come from disasm/symbols.txt.

The output is generated from your ROM, so it stays on your machine and is never
committed.

    python3 tools/disasm.py --rom "Top Gear (USA).sfc" --coverage recordings/coverage.tgcov \
        --out build/disasm
    (cd build/disasm && ca65 topgear.s -o topgear.o && ld65 -C topgear.cfg topgear.o -o topgear.sfc)
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tgcov import Coverage  # noqa: E402

ROM_SIZE = 0x80000
BANKS = ROM_SIZE // 0x8000
ROM_SHA256 = "ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788"

OPCODE, OPERAND, READ = 1, 2, 4
M16X16, M16X8, M8X16, M8X8, EMU = 8, 16, 32, 64, 128
FLOW_TARGET, CALL_TARGET = 1, 2

# Opcode table: mnemonic and addressing mode for all 256 opcodes.
_ROWS = """
brk:imm8 ora:dpix cop:imm8 ora:sr tsb:dp ora:dp asl:dp ora:dpil php:imp ora:immm asl:acc phd:imp tsb:abs ora:abs asl:abs ora:long
bpl:rel ora:dpiy ora:dpi ora:sriy trb:dp ora:dpx asl:dpx ora:dpily clc:imp ora:absy inc:acc tcs:imp trb:abs ora:absx asl:absx ora:longx
jsr:abs and:dpix jsl:long and:sr bit:dp and:dp rol:dp and:dpil plp:imp and:immm rol:acc pld:imp bit:abs and:abs rol:abs and:long
bmi:rel and:dpiy and:dpi and:sriy bit:dpx and:dpx rol:dpx and:dpily sec:imp and:absy dec:acc tsc:imp bit:absx and:absx rol:absx and:longx
rti:imp eor:dpix wdm:imm8 eor:sr mvp:blk eor:dp lsr:dp eor:dpil pha:imp eor:immm lsr:acc phk:imp jmp:abs eor:abs lsr:abs eor:long
bvc:rel eor:dpiy eor:dpi eor:sriy mvn:blk eor:dpx lsr:dpx eor:dpily cli:imp eor:absy phy:imp tcd:imp jml:long eor:absx lsr:absx eor:longx
rts:imp adc:dpix per:rell adc:sr stz:dp adc:dp ror:dp adc:dpil pla:imp adc:immm ror:acc rtl:imp jmp:absi adc:abs ror:abs adc:long
bvs:rel adc:dpiy adc:dpi adc:sriy stz:dpx adc:dpx ror:dpx adc:dpily sei:imp adc:absy ply:imp tdc:imp jmp:absix adc:absx ror:absx adc:longx
bra:rel sta:dpix brl:rell sta:sr sty:dp sta:dp stx:dp sta:dpil dey:imp bit:immm txa:imp phb:imp sty:abs sta:abs stx:abs sta:long
bcc:rel sta:dpiy sta:dpi sta:sriy sty:dpx sta:dpx stx:dpy sta:dpily tya:imp sta:absy txs:imp txy:imp stz:abs sta:absx stz:absx sta:longx
ldy:immx lda:dpix ldx:immx lda:sr ldy:dp lda:dp ldx:dp lda:dpil tay:imp lda:immm tax:imp plb:imp ldy:abs lda:abs ldx:abs lda:long
bcs:rel lda:dpiy lda:dpi lda:sriy ldy:dpx lda:dpx ldx:dpy lda:dpily clv:imp lda:absy tsx:imp tyx:imp ldy:absx lda:absx ldx:absy lda:longx
cpy:immx cmp:dpix rep:imm8 cmp:sr cpy:dp cmp:dp dec:dp cmp:dpil iny:imp cmp:immm dex:imp wai:imp cpy:abs cmp:abs dec:abs cmp:long
bne:rel cmp:dpiy cmp:dpi cmp:sriy pei:dpi cmp:dpx dec:dpx cmp:dpily cld:imp cmp:absy phx:imp stp:imp jml:absil cmp:absx dec:absx cmp:longx
cpx:immx sbc:dpix sep:imm8 sbc:sr cpx:dp sbc:dp inc:dp sbc:dpil inx:imp sbc:immm nop:imp xba:imp cpx:abs sbc:abs inc:abs sbc:long
beq:rel sbc:dpiy sbc:dpi sbc:sriy pea:pea sbc:dpx inc:dpx sbc:dpily sed:imp sbc:absy plx:imp xce:imp jsr:absix sbc:absx inc:absx sbc:longx
"""
OPS = [tuple(t.split(":")) for t in _ROWS.split()]
assert len(OPS) == 256

MODE_LEN = {
    "imp": 1, "acc": 1, "imm8": 2, "dp": 2, "dpx": 2, "dpy": 2, "dpi": 2, "dpix": 2,
    "dpiy": 2, "dpil": 2, "dpily": 2, "sr": 2, "sriy": 2, "rel": 2, "abs": 3, "absx": 3,
    "absy": 3, "absi": 3, "absix": 3, "absil": 3, "rell": 3, "blk": 3, "pea": 3,
    "long": 4, "longx": 4,
}


def insn_len(op, m8, x8):
    mode = OPS[op][1]
    if mode == "immm":
        return 2 if m8 else 3
    if mode == "immx":
        return 2 if x8 else 3
    return MODE_LEN[mode]


def widths(flag):
    """Register-width combinations (m8, x8) the instruction was executed with."""
    out = []
    if flag & M16X16: out.append((False, False))
    if flag & M16X8: out.append((False, True))
    if flag & M8X16: out.append((True, False))
    if flag & M8X8: out.append((True, True))
    return out


def load_coverage(path):
    cov = Coverage.load(path)
    return cov.flags, cov.entry, cov.db, [cov.dp_at(i) for i in range(ROM_SIZE)]


def load_symbols(path):
    """symbols.txt: '<bank>:<addr> <name> [; comment]' per line, e.g. '00:8000 Reset ; power-on'."""
    names, comments = {}, {}
    if not path or not os.path.exists(path):
        return names, comments
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        comment = None
        if ";" in line:
            line, comment = line.split(";", 1)
            comment = comment.strip()
        parts = line.split()
        b, a = parts[0].split(":")
        off = (int(b, 16) & 0x7f) * 0x8000 + (int(a, 16) & 0x7fff)
        if len(parts) > 1:
            names[off] = parts[1]
        if comment:
            comments[off] = comment
    return names, comments


# SNES hardware registers, for operand comments.
HW_REGS = dict(zip(range(0x2100, 0x2134), """INIDISP OBSEL OAMADDL OAMADDH OAMDATA BGMODE MOSAIC BG1SC BG2SC BG3SC BG4SC
BG12NBA BG34NBA BG1HOFS BG1VOFS BG2HOFS BG2VOFS BG3HOFS BG3VOFS BG4HOFS BG4VOFS VMAIN VMADDL VMADDH VMDATAL
VMDATAH M7SEL M7A M7B M7C M7D M7X M7Y CGADD CGDATA W12SEL W34SEL WOBJSEL WH0 WH1 WH2 WH3 WBGLOG WOBJLOG TM TS
TMW TSW CGWSEL CGADSUB COLDATA SETINI""".split()))
HW_REGS.update(zip(range(0x2134, 0x2140), "MPYL MPYM MPYH SLHV OAMDATAREAD VMDATALREAD VMDATAHREAD CGDATAREAD OPHCT OPVCT STAT77 STAT78".split()))
HW_REGS.update({0x2140: "APUIO0", 0x2141: "APUIO1", 0x2142: "APUIO2", 0x2143: "APUIO3",
                0x2180: "WMDATA", 0x2181: "WMADDL", 0x2182: "WMADDM", 0x2183: "WMADDH", 0x4016: "JOYSER0", 0x4017: "JOYSER1"})
HW_REGS.update(zip(range(0x4200, 0x420E), "NMITIMEN WRIO WRMPYA WRMPYB WRDIVL WRDIVH WRDIVB HTIMEL HTIMEH VTIMEL VTIMEH MDMAEN HDMAEN MEMSEL".split()))
HW_REGS.update(zip(range(0x4210, 0x4220), "RDNMI TIMEUP HVBJOY RDIO RDDIVL RDDIVH RDMPYL RDMPYH JOY1L JOY1H JOY2L JOY2H JOY3L JOY3H JOY4L JOY4H".split()))
for _ch in range(8):
    for _i, _n in enumerate("DMAP BBAD A1TL A1TH A1B DASL DASH DASB A2AL A2AH NLTR".split()):
        HW_REGS[0x4300 + _ch * 16 + _i] = f"{_n}{_ch}"


def load_ram_names(path):
    """ram.txt: '<addr> <name> [; comment]' with addr as 0000-1FFF (low RAM) or 7E0000-7FFFFF."""
    names = {}
    if not path or not os.path.exists(path):
        return names
    for line in open(path):
        line = line.split(";", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        a, name = line.split()[:2]
        a = int(a, 16)
        if a < 0x2000:
            a |= 0x7E0000
        names[a] = name
    return names


def ram_name(names, addr):
    """Name for a work-RAM address, allowing an offset into the previous variable."""
    if addr in names:
        return names[addr]
    if addr - 1 in names:
        return names[addr - 1] + "+1"
    return None


def cpu_addr(off):
    return ((off >> 15) << 16) | 0x8000 | (off & 0x7fff)


def rel_expr(delta):
    """Branch target relative to the instruction start, for targets without a label."""
    return f"*{delta:+d}"


def rom_off(bank, addr):
    if addr < 0x8000 or (bank & 0x7e) == 0x7e:
        return None
    return ((bank & 0x7f) * 0x8000 + (addr & 0x7fff)) % ROM_SIZE


class Disassembler:
    def __init__(self, rom, flags, entry, names, comments, db=None, dp=None, ram_names=None):
        self.rom, self.flags, self.entry = rom, flags, entry
        self.db, self.dp, self.ram_names = db, dp, ram_names or {}
        self.names, self.comments = names, comments
        self.insns = {}  # offset -> (length, m8, x8)
        self.notes = {}  # offset -> why it was left as data
        self.labels = {}  # offset -> name

    def decide(self):
        rom, flags = self.rom, self.flags
        for off in range(ROM_SIZE):
            f = flags[off]
            if not f & OPCODE:
                continue
            ws = widths(f)
            op = rom[off]
            lens = {insn_len(op, m, x) for m, x in ws} or {insn_len(op, True, True)}
            if len(lens) != 1:
                self.notes[off] = "executed with different register widths"
                continue
            n = lens.pop()
            if (off & 0x7fff) + n > 0x8000:
                self.notes[off] = "crosses a bank boundary"
                continue
            m8, x8 = ws[0] if ws else (True, True)
            self.insns[off] = (n, m8, x8)
        # An instruction whose bytes overlap another executed instruction stays data.
        claimed = bytearray(ROM_SIZE)
        for off in sorted(self.insns):
            n = self.insns[off][0]
            if any(claimed[off + i] for i in range(n)):
                self.notes[off] = "overlaps another instruction"
                continue
            for i in range(n):
                claimed[off + i] = 1 if i == 0 else 2
        for off in list(self.insns):
            if off in self.notes:
                del self.insns[off]
        self.inside = bytearray(ROM_SIZE)  # 1 = operand byte, cannot carry a label
        for off, (n, _, _) in self.insns.items():
            for i in range(1, n):
                self.inside[off + i] = 1

        def want(off, prefix):
            if off is None or self.inside[off]:
                return
            if off not in self.labels:
                a = cpu_addr(off)
                self.labels[off] = self.names.get(off) or f"{prefix}{a >> 16:02X}_{a & 0xffff:04X}"

        for off in self.names:
            want(off, "")
        for off in range(ROM_SIZE):
            if self.entry[off] & CALL_TARGET and off in self.insns:
                want(off, "F")
        for off in range(ROM_SIZE):
            if self.entry[off] & FLOW_TARGET and off in self.insns:
                want(off, "L")
        for off, (n, _, _) in self.insns.items():
            t = self.static_target(off)
            if t is not None:
                want(t, "L")

    def static_target(self, off):
        """ROM offset of a branch/jump/call target known from the instruction alone."""
        op = self.rom[off]
        mnem, mode = OPS[op]
        a = cpu_addr(off)
        bank, pc = a >> 16, a & 0xffff
        b = self.rom
        if mode == "rel":
            d = b[off + 1] - 256 if b[off + 1] >= 128 else b[off + 1]
            return rom_off(bank, (pc + 2 + d) & 0xffff)
        if mode == "rell" and mnem == "brl":
            d = b[off + 1] | b[off + 2] << 8
            d = d - 65536 if d >= 32768 else d
            return rom_off(bank, (pc + 3 + d) & 0xffff)
        if mode == "abs" and mnem in ("jmp", "jsr"):
            return rom_off(bank, b[off + 1] | b[off + 2] << 8)
        if mode == "long" and mnem in ("jml", "jsl"):
            return rom_off(b[off + 3], b[off + 1] | b[off + 2] << 8)
        return None

    def label_ref(self, target, bank_of_insn):
        """Label for an operand if it can be used in a same-bank 16-bit operand."""
        if target is None or target not in self.labels:
            return None
        if (target >> 15) != bank_of_insn:
            return None
        return self.labels[target]

    def ram_comment(self, off, mode, v8, v16, v24):
        """Name of the work-RAM variable an instruction touches, when it is known."""
        addr = None
        if mode in ("dp", "dpx", "dpy", "dpi", "dpix", "dpiy", "dpil", "dpily") and self.dp is not None:
            addr = 0x7E0000 | ((self.dp[off] + v8) & 0xffff)  # for indirect modes: the pointer
        elif mode in ("abs", "absx", "absy") and OPS[self.rom[off]][0] not in ("jmp", "jsr"):
            db = self.db[off] if self.db is not None else 0
            if (db & 0x40) == 0 and v16 in HW_REGS:
                return HW_REGS[v16]
            if db in (0x7E, 0x7F):
                addr = (db << 16) | v16
            elif (db & 0x40) == 0 and v16 < 0x2000:
                addr = 0x7E0000 | v16
        elif mode in ("long", "longx") and (v24 >> 16) in (0x7E, 0x7F):
            addr = v24
        elif mode in ("long", "longx") and (v24 >> 16) & 0x40 == 0 and (v24 & 0xffff) in HW_REGS:
            return HW_REGS[v24 & 0xffff]
        if addr is None:
            return None
        return ram_name(self.ram_names, addr)

    def operand(self, off, n):
        b = self.rom
        op = b[off]
        mnem, mode = OPS[op]
        bank = off >> 15
        a = cpu_addr(off)
        v8 = b[off + 1] if n > 1 else 0
        v16 = v8 | (b[off + 2] << 8 if n > 2 else 0)
        v24 = v16 | (b[off + 3] << 16 if n > 3 else 0)
        target = self.static_target(off)
        lab = self.label_ref(target, bank)
        comment = self.ram_comment(off, mode, v8, v16, v24)
        if mode == "imp":
            return mnem, comment
        if mode == "acc":
            return f"{mnem} a", comment
        if mode in ("immm", "immx"):
            return (f"{mnem} #${v8:02X}" if n == 2 else f"{mnem} #${v16:04X}"), comment
        if mode == "imm8":
            if mnem in ("rep", "sep"):
                return f"{mnem} #${v8:02X}", comment
            return None, comment  # brk/cop/wdm: emitted as bytes
        if mode == "rel":
            delta = 2 + (v8 - 256 if v8 >= 128 else v8)
            return f"{mnem} {lab or rel_expr(delta)}", comment
        if mode == "rell":
            d = v16 - 65536 if v16 >= 32768 else v16
            return f"{mnem} {lab or rel_expr(3 + d)}", comment
        if mode == "blk":
            return f"{mnem} #${b[off + 2]:02X},#${v8:02X}", comment
        if mode == "pea":
            return f"pea ${v16:04X}", comment
        if mode == "dp": return f"{mnem} z:${v8:02X}", comment
        if mode == "dpx": return f"{mnem} z:${v8:02X},x", comment
        if mode == "dpy": return f"{mnem} z:${v8:02X},y", comment
        if mode == "dpi": return f"{mnem} (${v8:02X})", comment
        if mode == "dpix": return f"{mnem} (${v8:02X},x)", comment
        if mode == "dpiy": return f"{mnem} (${v8:02X}),y", comment
        if mode == "dpil": return f"{mnem} [${v8:02X}]", comment
        if mode == "dpily": return f"{mnem} [${v8:02X}],y", comment
        if mode == "sr": return f"{mnem} ${v8:02X},s", comment
        if mode == "sriy": return f"{mnem} (${v8:02X},s),y", comment
        if mode == "abs":
            if mnem in ("jmp", "jsr"):
                return f"{mnem} {lab or f'${v16:04X}'}", comment
            return f"{mnem} a:${v16:04X}", comment
        if mode == "absx": return f"{mnem} a:${v16:04X},x", comment
        if mode == "absy": return f"{mnem} a:${v16:04X},y", comment
        if mode == "absi": return f"jmp (${v16:04X})", comment
        if mode == "absix": return f"{mnem} (${v16:04X},x)", comment
        if mode == "absil": return f"jml [${v16:04X}]", comment
        if mode == "long":
            if mnem in ("jml", "jsl"):
                if target is not None and target in self.labels:
                    comment = self.labels[target]
                return f"{mnem} ${v24:06X}", comment
            return f"{mnem} f:${v24:06X}", comment
        if mode == "longx":
            return f"{mnem} f:${v24:06X},x", comment
        raise ValueError(mode)

    def write(self, out_dir):
        os.makedirs(out_dir, exist_ok=True)
        lines = [
            "; Generated by tools/disasm.py from your ROM. Do not commit or distribute.",
            ".p816",
            ".smart -",
            "",
        ]
        stats = {"insn": 0, "data": 0}
        for bank in range(BANKS):
            lines += [f'.segment "BANK{bank:02X}"', ""]
            cur_m = cur_x = None
            data = []

            def flush():
                for i in range(0, len(data), 16):
                    chunk = data[i:i + 16]
                    lines.append("  .byte " + ",".join(f"${x:02X}" for x in chunk))
                data.clear()

            off = bank * 0x8000
            end = off + 0x8000
            while off < end:
                if off in self.labels or off in self.comments or off in self.notes:
                    flush()
                    if off in self.labels:
                        lines.append(f"{self.labels[off]}:")
                    if off in self.comments:
                        lines.append(f"  ; {self.comments[off]}")
                    if off in self.notes:
                        lines.append(f"  ; data: {self.notes[off]}")
                if off in self.insns:
                    n, m8, x8 = self.insns[off]
                    text, comment = self.operand(off, n)
                    flush()
                    if text is None:
                        lines.append("  .byte " + ",".join(f"${self.rom[off + i]:02X}" for i in range(n)) + f"  ; {OPS[self.rom[off]][0]}")
                    else:
                        if OPS[self.rom[off]][1] == "immm" and m8 != cur_m:
                            lines.append("  .a8" if m8 else "  .a16")
                            cur_m = m8
                        if OPS[self.rom[off]][1] == "immx" and x8 != cur_x:
                            lines.append("  .i8" if x8 else "  .i16")
                            cur_x = x8
                        a = cpu_addr(off)
                        lines.append(f"  {text:<28}; {a >> 16:02X}:{a & 0xffff:04X}" + (f" {comment}" if comment else ""))
                    stats["insn"] += 1
                    off += n
                else:
                    data.append(self.rom[off])
                    stats["data"] += 1
                    off += 1
            flush()
            lines.append("")
        with open(os.path.join(out_dir, "topgear.s"), "w") as f:
            f.write("\n".join(lines) + "\n")
        cfg = ["MEMORY {"]
        cfg += [f"  ROM{b:02X}: start = $8000, size = $8000, fill = yes;" for b in range(BANKS)]
        cfg += ["}", "SEGMENTS {"]
        cfg += [f"  BANK{b:02X}: load = ROM{b:02X}, type = ro;" for b in range(BANKS)]
        cfg += ["}"]
        with open(os.path.join(out_dir, "topgear.cfg"), "w") as f:
            f.write("\n".join(cfg) + "\n")
        return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--coverage", required=True)
    ap.add_argument("--symbols", default=os.path.join(os.path.dirname(__file__), "..", "disasm", "symbols.txt"))
    ap.add_argument("--ram", default=os.path.join(os.path.dirname(__file__), "..", "disasm", "ram.txt"))
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    if len(rom) % 1024 == 512:
        rom = rom[512:]
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        sys.exit("not an unmodified Top Gear (USA) ROM")
    flags, entry, db, dp = load_coverage(args.coverage)
    names, comments = load_symbols(args.symbols)
    d = Disassembler(rom, flags, entry, names, comments, db, dp, load_ram_names(args.ram))
    d.decide()
    stats = d.write(args.out)
    print(f"{stats['insn']} instructions, {stats['data']} data bytes, {len(d.labels)} labels, "
          f"{len(d.notes)} executed spots left as data")


if __name__ == "__main__":
    main()
