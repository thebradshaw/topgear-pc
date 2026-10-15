"""Reads and writes coverage files (.tgcov) produced by src/coverage.c."""
import struct

ROM = 0x80000
RAM = 0x20000
RAM_OPS = 16384
V1, V2 = b"TGCOV01\0", b"TGCOV02\0"


class Coverage:
    def __init__(self):
        self.flags = bytearray(ROM)
        self.entry = bytearray(ROM)
        self.db = bytearray(ROM)
        self.dp = bytearray(2 * ROM)
        self.ram = bytearray(RAM)
        self.ram_ops = set()  # (ram_addr, opcode, mode_bits)

    @classmethod
    def load(cls, path):
        d = open(path, "rb").read()
        if d[:8] not in (V1, V2):
            raise ValueError(f"{path}: not a coverage file")
        c = cls()
        o = 8
        for name, n in (("flags", ROM), ("entry", ROM), ("db", ROM), ("dp", 2 * ROM), ("ram", RAM)):
            setattr(c, name, bytearray(d[o:o + n]))
            o += n
        if d[:8] == V2:
            for (k,) in struct.iter_unpack("<I", d[o:o + 4 * RAM_OPS]):
                if k:
                    c.ram_ops.add(((k >> 13) & 0x1FFFF, (k >> 5) & 0xFF, k & 0x1F))
        return c

    def dp_at(self, i):
        return self.dp[2 * i] | self.dp[2 * i + 1] << 8

    def save(self, path):
        table = [0] * RAM_OPS
        for addr, op, mode in sorted(self.ram_ops):
            key = 1 << 31 | addr << 13 | op << 5 | mode
            h = ((key * 2654435761) & 0xFFFFFFFF) >> 18
            for i in range(RAM_OPS):
                j = (h + i) & (RAM_OPS - 1)
                if table[j] == 0:
                    table[j] = key
                    break
        with open(path, "wb") as f:
            f.write(V2)
            for b in (self.flags, self.entry, self.db, self.dp, self.ram):
                f.write(b)
            f.write(struct.pack(f"<{RAM_OPS}I", *table))

    def merge(self, other):
        old = bytes(self.flags)
        for i in range(ROM):
            if other.flags[i] & 1 and not old[i] & 1:
                self.db[i] = other.db[i]
                self.dp[2 * i:2 * i + 2] = other.dp[2 * i:2 * i + 2]
            self.flags[i] |= other.flags[i]
            self.entry[i] |= other.entry[i]
        for i in range(RAM):
            self.ram[i] |= other.ram[i]
        self.ram_ops |= other.ram_ops
