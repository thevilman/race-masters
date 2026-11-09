#!/usr/bin/env python3
"""A small SPC700 disassembler, to read the sound driver in an .spc snapshot.

    python3 tools/audio/spc700_disasm.py song_0.spc 0x0800 0x0900

Operand notation: d = direct page byte, !a = absolute word, #i = immediate,
r = relative branch (shown as its target), m.b = 13-bit address and bit number.
"""
import sys

# mnemonic templates; operand letters are filled from the instruction bytes.
_OPS = """
NOP|TCALL 0|SET1 d.0|BBS d.0,r|OR A,d|OR A,!a|OR A,(X)|OR A,[d+X]|OR A,#i|OR D,S|OR1 C,m|ASL d|ASL !a|PUSH PSW|TSET1 !a|BRK
BPL r|TCALL 1|CLR1 d.0|BBC d.0,r|OR A,d+X|OR A,!a+X|OR A,!a+Y|OR A,[d]+Y|OR D,#I|OR (X),(Y)|DECW d|ASL d+X|ASL A|DEC X|CMP X,!a|JMP [!a+X]
CLRP|TCALL 2|SET1 d.1|BBS d.1,r|AND A,d|AND A,!a|AND A,(X)|AND A,[d+X]|AND A,#i|AND D,S|OR1 C,/m|ROL d|ROL !a|PUSH A|CBNE d,r|BRA r
BMI r|TCALL 3|CLR1 d.1|BBC d.1,r|AND A,d+X|AND A,!a+X|AND A,!a+Y|AND A,[d]+Y|AND D,#I|AND (X),(Y)|INCW d|ROL d+X|ROL A|INC X|CMP X,d|CALL !a
SETP|TCALL 4|SET1 d.2|BBS d.2,r|EOR A,d|EOR A,!a|EOR A,(X)|EOR A,[d+X]|EOR A,#i|EOR D,S|AND1 C,m|LSR d|LSR !a|PUSH X|TCLR1 !a|PCALL u
BVC r|TCALL 5|CLR1 d.2|BBC d.2,r|EOR A,d+X|EOR A,!a+X|EOR A,!a+Y|EOR A,[d]+Y|EOR D,#I|EOR (X),(Y)|CMPW YA,d|LSR d+X|LSR A|MOV X,A|CMP Y,!a|JMP !a
CLRC|TCALL 6|SET1 d.3|BBS d.3,r|CMP A,d|CMP A,!a|CMP A,(X)|CMP A,[d+X]|CMP A,#i|CMP D,S|AND1 C,/m|ROR d|ROR !a|PUSH Y|DBNZ d,r|RET
BVS r|TCALL 7|CLR1 d.3|BBC d.3,r|CMP A,d+X|CMP A,!a+X|CMP A,!a+Y|CMP A,[d]+Y|CMP D,#I|CMP (X),(Y)|ADDW YA,d|ROR d+X|ROR A|MOV A,X|CMP Y,d|RETI
SETC|TCALL 8|SET1 d.4|BBS d.4,r|ADC A,d|ADC A,!a|ADC A,(X)|ADC A,[d+X]|ADC A,#i|ADC D,S|EOR1 C,m|DEC d|DEC !a|MOV Y,#i|POP PSW|MOV D,#I
BCC r|TCALL 9|CLR1 d.4|BBC d.4,r|ADC A,d+X|ADC A,!a+X|ADC A,!a+Y|ADC A,[d]+Y|ADC D,#I|ADC (X),(Y)|SUBW YA,d|DEC d+X|DEC A|MOV X,SP|DIV YA,X|XCN A
EI|TCALL 10|SET1 d.5|BBS d.5,r|SBC A,d|SBC A,!a|SBC A,(X)|SBC A,[d+X]|SBC A,#i|SBC D,S|MOV1 C,m|INC d|INC !a|CMP Y,#i|POP A|MOV (X)+,A
BCS r|TCALL 11|CLR1 d.5|BBC d.5,r|SBC A,d+X|SBC A,!a+X|SBC A,!a+Y|SBC A,[d]+Y|SBC D,#I|SBC (X),(Y)|MOVW YA,d|INC d+X|INC A|MOV SP,X|DAS A|MOV A,(X)+
DI|TCALL 12|SET1 d.6|BBS d.6,r|MOV d,A|MOV !a,A|MOV (X),A|MOV [d+X],A|CMP X,#i|MOV !a,X|MOV1 m,C|MOV d,Y|MOV !a,Y|MOV X,#i|POP X|MUL YA
BNE r|TCALL 13|CLR1 d.6|BBC d.6,r|MOV d+X,A|MOV !a+X,A|MOV !a+Y,A|MOV [d]+Y,A|MOV d,X|MOV d+Y,X|MOVW d,YA|MOV d+X,Y|DEC Y|MOV A,Y|CBNE d+X,r|DAA A
CLRV|TCALL 14|SET1 d.7|BBS d.7,r|MOV A,d|MOV A,!a|MOV A,(X)|MOV A,[d+X]|MOV A,#i|MOV X,!a|NOT1 m|MOV Y,d|MOV Y,!a|NOTC|POP Y|SLEEP
BEQ r|TCALL 15|CLR1 d.7|BBC d.7,r|MOV A,d+X|MOV A,!a+X|MOV A,!a+Y|MOV A,[d]+Y|MOV X,d|MOV X,d+Y|MOVW d,YA|MOV Y,d+X|INC Y|MOV Y,A|DBNZ Y,r|STOP
"""
OPS = [op for line in _OPS.strip().splitlines() for op in line.split("|")]
assert len(OPS) == 256, len(OPS)
OPS[0xFA] = "MOV D,S"  # the table above repeats MOVW by mistake for $FA


def _has_m(t):
    return " m" in t or "/m" in t


def length(op):
    t = OPS[op]
    if "TCALL" in t:
        return 1
    n = 1
    if "!a" in t or _has_m(t) or "D,S" in t or "D,#I" in t:
        n += 2
    if "d" in t:
        n += 1
    if "#i" in t or " u" in t:
        n += 1
    if t.endswith("r") and (t.endswith(",r") or t.endswith(" r")):
        n += 1
    return n


def decode(mem, pc):
    op = mem[pc]
    s = OPS[op]
    b = [mem[(pc + i) & 0xFFFF] for i in range(1, 4)]
    n = length(op)
    if "TCALL" in s:
        return n, s
    i = 0
    if "D,S" in s:
        s = s.replace("D,S", "$%02X,$%02X" % (b[1], b[0]))
        i = 2
    elif "D,#I" in s:
        s = s.replace("D,#I", "$%02X,#$%02X" % (b[1], b[0]))
        i = 2
    elif "!a" in s:
        s = s.replace("!a", "!$%04X" % (b[0] | b[1] << 8))
        i = 2
    elif _has_m(s):
        w = b[0] | b[1] << 8
        s = s.replace("m", "$%04X.%d" % (w & 0x1FFF, w >> 13), 1)
        i = 2
    if "d" in s:
        s = s.replace("d", "$%02X" % b[i], 1)
        i += 1
    if "#i" in s:
        s = s.replace("#i", "#$%02X" % b[i])
        i += 1
    if " u" in s:
        s = s.replace(" u", " $FF%02X" % b[i])
        i += 1
    if s.endswith(",r") or s.endswith(" r"):
        off = b[i]
        off = off - 256 if off >= 128 else off
        s = s[:-1] + "$%04X" % ((pc + n + off) & 0xFFFF)
    return n, s


def main():
    path = sys.argv[1]
    start = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0)
    data = open(path, "rb").read()
    mem = data[0x100:0x10100] if data[:4] == b"SNES" else data
    pc = start
    while pc < end:
        n, s = decode(mem, pc)
        raw = " ".join("%02X" % mem[(pc + i) & 0xFFFF] for i in range(n))
        print("%04X  %-9s %s" % (pc, raw, s))
        pc += n


if __name__ == "__main__":
    main()
