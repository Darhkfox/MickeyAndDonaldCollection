"""Minimal Z80 disassembler (enough for the Castle of Illusion sound driver).

    python coi_game_z80dis.py 0x0000 60          # disassemble the driver image as loaded in Z80 RAM
    from coi_game_z80dis import disz80, z80_image

z80_image() rebuilds the Z80 RAM image exactly as the 68000 routine $79EE0 uploads it:
    ROM $784B0..$7939F -> Z80 $0000..$0EEF   (driver code)
    ROM $793A0..$79C1F -> Z80 $0F00..$177F   (data)
    ROM $79C20..$79EDF -> Z80 $1900..$1BBF   (data)
    ROM $79F76..$79F84 -> Z80 $1C00..$1C0E   (mailbox / state init)
"""
import os
import sys

R8 = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add a,', 'adc a,', 'sub ', 'sbc a,', 'and ', 'xor ', 'or ', 'cp ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']


def z80_image():
    here = os.path.dirname(os.path.abspath(__file__))
    rom = open(os.path.join(os.path.dirname(os.path.dirname(here)),
                            'Mickey Mouse - Castle of Illusion (U) [!].bin'), 'rb').read()
    img = bytearray(0x2000)
    img[0x0000:0x0EF0] = rom[0x784B0:0x784B0 + 0xEF0]
    img[0x0F00:0x1780] = rom[0x793A0:0x793A0 + 0x880]
    img[0x1900:0x1BC0] = rom[0x79C20:0x79C20 + 0x2C0]
    img[0x1C00:0x1C0F] = rom[0x79F76:0x79F76 + 0xF]
    return bytes(img)


def _d(b):
    return b - 256 if b & 0x80 else b


def dis1(mem, pc):
    """Disassemble one instruction; returns (size, text)."""
    def b(i):
        return mem[(pc + i) & 0xFFFF]

    def w(i):
        return b(i) | (b(i + 1) << 8)

    op = b(0)
    ix = None
    p = 0
    if op in (0xDD, 0xFD):
        ix = 'ix' if op == 0xDD else 'iy'
        p = 1
        op = b(1)
        if op in (0xDD, 0xFD, 0xED):
            return 1, 'db $%02x' % b(0)

    def reg(r, dispidx):
        # returns (text, extra bytes consumed)
        if ix:
            if r == 6:
                return '(%s%+d)' % (ix, _d(b(dispidx))), 1
            if r == 4:
                return ix + 'h', 0
            if r == 5:
                return ix + 'l', 0
        return R8[r], 0

    def rp(i):
        return ix if (ix and i == 2) else RP[i]

    if op == 0xCB:
        if ix:
            d = _d(b(p + 1))
            o = b(p + 2)
            x, y, z = o >> 6, (o >> 3) & 7, o & 7
            m = '(%s%+d)' % (ix, d)
            t = [ROT[y] + ' ' + m, 'bit %d,%s' % (y, m), 'res %d,%s' % (y, m), 'set %d,%s' % (y, m)][x]
            return p + 3, t
        o = b(1)
        x, y, z = o >> 6, (o >> 3) & 7, o & 7
        t = [ROT[y] + ' ' + R8[z], 'bit %d,%s' % (y, R8[z]), 'res %d,%s' % (y, R8[z]), 'set %d,%s' % (y, R8[z])][x]
        return 2, t
    if op == 0xED and not ix:
        o = b(1)
        x, y, z, q, pp = o >> 6, (o >> 3) & 7, o & 7, (o >> 3) & 1, (o >> 4) & 3
        if x == 1:
            if z == 0:
                return 2, 'in %s,(c)' % (R8[y] if y != 6 else 'f')
            if z == 1:
                return 2, 'out (c),%s' % (R8[y] if y != 6 else '0')
            if z == 2:
                return 2, ('sbc hl,' if q == 0 else 'adc hl,') + RP[pp]
            if z == 3:
                return 4, ('ld ($%04x),%s' % (w(2), RP[pp])) if q == 0 else ('ld %s,($%04x)' % (RP[pp], w(2)))
            if z == 4:
                return 2, 'neg'
            if z == 5:
                return 2, 'retn' if y != 1 else 'reti'
            if z == 6:
                return 2, 'im %s' % ['0', '0/1', '1', '2'][y & 3]
            return 2, ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld', 'nop', 'nop'][y]
        if x == 2 and z <= 3 and y >= 4:
            return 2, [['ldi', 'cpi', 'ini', 'outi'], ['ldd', 'cpd', 'ind', 'outd'],
                       ['ldir', 'cpir', 'inir', 'otir'], ['lddr', 'cpdr', 'indr', 'otdr']][y - 4][z]
        return 2, 'db $ed,$%02x' % o

    x, y, z, q, pp = op >> 6, (op >> 3) & 7, op & 7, (op >> 3) & 1, (op >> 4) & 3
    n = p + 1  # index of the byte after the opcode
    if x == 0:
        if z == 0:
            if y == 0:
                return n, 'nop'
            if y == 1:
                return n, "ex af,af'"
            if y == 2:
                return n + 1, 'djnz $%04x' % ((pc + n + 1 + _d(b(n))) & 0xFFFF)
            if y == 3:
                return n + 1, 'jr $%04x' % ((pc + n + 1 + _d(b(n))) & 0xFFFF)
            return n + 1, 'jr %s,$%04x' % (CC[y - 4], (pc + n + 1 + _d(b(n))) & 0xFFFF)
        if z == 1:
            if q == 0:
                return n + 2, 'ld %s,$%04x' % (rp(pp), w(n))
            return n, 'add %s,%s' % (rp(2), rp(pp))
        if z == 2:
            t = [['ld (bc),a', 'ld (de),a', 'ld ($%04x),%s' % (w(n), rp(2)), 'ld ($%04x),a' % w(n)],
                 ['ld a,(bc)', 'ld a,(de)', 'ld %s,($%04x)' % (rp(2), w(n)), 'ld a,($%04x)' % w(n)]][q][pp]
            return n + (2 if pp >= 2 else 0), t
        if z == 3:
            return n, ('inc ' if q == 0 else 'dec ') + rp(pp)
        if z in (4, 5):
            r, e = reg(y, n)
            return n + e, ('inc ' if z == 4 else 'dec ') + r
        if z == 6:
            r, e = reg(y, n)
            return n + e + 1, 'ld %s,$%02x' % (r, b(n + e))
        return n, ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf', 'ccf'][y]
    if x == 1:
        if op == 0x76:
            return n, 'halt'
        # with index prefix, (ix+d) replaces (hl) but h/l stay h/l when the other operand is (ix+d)
        if ix and (y == 6 or z == 6):
            d = _d(b(n))
            m = '(%s%+d)' % (ix, d)
            if y == 6:
                return n + 1, 'ld %s,%s' % (m, R8[z])
            return n + 1, 'ld %s,%s' % (R8[y], m)
        r1, _ = reg(y, n)
        r2, _ = reg(z, n)
        return n, 'ld %s,%s' % (r1, r2)
    if x == 2:
        r, e = reg(z, n)
        return n + e, ALU[y] + r
    # x == 3
    if z == 0:
        return n, 'ret ' + CC[y]
    if z == 1:
        if q == 0:
            return n, 'pop ' + (ix if (ix and pp == 2) else RP2[pp])
        return n, ['ret', 'exx', 'jp (%s)' % (ix or 'hl'), 'ld sp,%s' % (ix or 'hl')][pp]
    if z == 2:
        return n + 2, 'jp %s,$%04x' % (CC[y], w(n))
    if z == 3:
        if y == 0:
            return n + 2, 'jp $%04x' % w(n)
        if y == 2:
            return n + 1, 'out ($%02x),a' % b(n)
        if y == 3:
            return n + 1, 'in a,($%02x)' % b(n)
        return n, ['', '', '', '', 'ex (sp),%s' % (ix or 'hl'), 'ex de,hl', 'di', 'ei'][y]
    if z == 4:
        return n + 2, 'call %s,$%04x' % (CC[y], w(n))
    if z == 5:
        if q == 0:
            return n, 'push ' + (ix if (ix and pp == 2) else RP2[pp])
        if pp == 0:
            return n + 2, 'call $%04x' % w(n)
        return n, 'db $%02x' % op
    if z == 6:
        return n + 1, ALU[y] + '$%02x' % b(n)
    return n, 'rst $%02x' % (y * 8)


def disz80(mem, addr, count=40):
    out = []
    pc = addr
    for _ in range(count):
        sz, t = dis1(mem, pc)
        raw = ' '.join('%02x' % mem[(pc + i) & 0xFFFF] for i in range(sz))
        out.append('%04x: %-14s %s' % (pc, raw, t))
        pc = (pc + sz) & 0xFFFF
    return '\n'.join(out)


def find_refs(mem, target, lo=0, hi=0x0F00):
    """Instructions in [lo,hi) whose text mentions $target (linear sweep from lo)."""
    res = []
    pc = lo
    key = '$%04x' % target
    while pc < hi:
        sz, t = dis1(mem, pc)
        if key in t:
            res.append((pc, t))
        pc += sz
    return res


if __name__ == '__main__':
    img = z80_image()
    if len(sys.argv) > 1 and sys.argv[1] == 'refs':
        for a in sys.argv[2:]:
            print('==', a)
            for pc, t in find_refs(img, int(a, 0)):
                print('  %04x  %s' % (pc, t))
    else:
        a = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
        print(disz80(img, a, n))
