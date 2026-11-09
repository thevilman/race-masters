"""Reads and writes coverage files (.tgcov) produced by src/coverage.c.

The RAM opcode set holds (RAM address, opcode, register widths) for code the game runs from
work RAM. The opcode is a byte of the game's code, so the repository's copy
(disasm/coverage.tgcov) is saved without it (strip(), `covmerge.py --strip`): such entries
keep the address and widths and have bit 30 set in their key, opcode 0. Coverage files from
your own runs (recordings/coverage.tgcov, tg_headless --coverage) keep the opcodes and stay
local.

    python3 tools/tgcov.py --check FILE   exits 1 if FILE still holds opcode bytes
"""
import struct
import sys

ROM = 0x80000
RAM = 0x20000
RAM_OPS = 16384
V1, V2 = b"TGCOV01\0", b"TGCOV02\0"
STRIPPED = 1 << 30  # a RAM opcode entry saved without its opcode


class Coverage:
    def __init__(self):
        self.flags = bytearray(ROM)
        self.entry = bytearray(ROM)
        self.db = bytearray(ROM)
        self.dp = bytearray(2 * ROM)
        self.ram = bytearray(RAM)
        self.ram_ops = set()  # (ram_addr, opcode or None when not kept, mode_bits)

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
                    op = None if k & STRIPPED else (k >> 5) & 0xFF
                    c.ram_ops.add(((k >> 13) & 0x1FFFF, op, k & 0x1F))
        return c

    def dp_at(self, i):
        return self.dp[2 * i] | self.dp[2 * i + 1] << 8

    def strip(self):
        """Drops the opcode bytes from the RAM opcode set (keeps addresses and widths)."""
        self.ram_ops = {(addr, None, mode) for addr, _, mode in self.ram_ops}

    def save(self, path):
        table = [0] * RAM_OPS
        for addr, op, mode in sorted(self.ram_ops, key=lambda e: (e[0], -1 if e[1] is None else e[1], e[2])):
            key = 1 << 31 | addr << 13 | mode | (STRIPPED if op is None else op << 5)
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


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--check":
        sys.exit(__doc__)
    kept = sum(1 for _, op, _ in Coverage.load(sys.argv[2]).ram_ops if op is not None)
    if kept:
        sys.exit(f"{sys.argv[2]}: {kept} RAM opcode entries keep the game's opcode byte; "
                 "save it with tools/covmerge.py --strip")
    print(f"{sys.argv[2]}: no opcode bytes")
