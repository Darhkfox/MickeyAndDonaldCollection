"""Minimal Z80 disassembler (enough for reading the QuackShot sound driver).

    python qs_game_z80dis.py 0x0000 80          # disassemble the driver as loaded in Z80 RAM
    from qs_game_z80dis import z80_driver_image, disasm
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qs_game_data import R  # noqa: E402

R8 = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
R16 = ['bc', 'de', 'hl', 'sp']
R16AF = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add a,', 'adc a,', 'sub ', 'sbc a,', 'and ', 'xor ', 'or ', 'cp ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']


def z80_driver_image(ending=False):
    """Z80 RAM image as written by $7FE00 (normal) or $7FE16 (ending bank)."""
    img = bytearray(0x2000)
    img[0:0xDC6] = R[0x784E0:0x784E0 + 0xDC6]
    if ending:
        img[0xF00:0xF00 + 0x6D8] = R[0x7A0C4:0x7A0C4 + 0x6D8]
    else:
        img[0xDD0:0xDD0 + 0xE1E] = R[0x792A6:0x792A6 + 0xE1E]
    return img


def _one(m, pc):
    """Decode one instruction at pc in memory m. Returns (length, text)."""
    def b(i):
        return m[(pc + i) & 0xFFFF] if pc + i < len(m) else 0

    def w(i):
        return b(i) | (b(i + 1) << 8)

    op = b(0)
    idx = None
    off = 0
    if op in (0xDD, 0xFD):
        idx = 'ix' if op == 0xDD else 'iy'
        off = 1
        op = b(1)
        if op in (0xDD, 0xFD, 0xED):
            return 1, 'db $%02x' % b(0)

    def r8(n, dpos):
        if n == 6 and idx:
            d = b(dpos)
            d = d - 256 if d & 0x80 else d
            return '(%s%+d)' % (idx, d)
        if idx and n in (4, 5):
            return idx + ('h' if n == 4 else 'l')
        return R8[n]

    def rp(n):
        if n == 2 and idx:
            return idx
        return R16[n]

    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    L = off + 1
    hasd = idx is not None and (op in (0x34, 0x35, 0x36) or (x == 1 and (y == 6 or z == 6) and op != 0x76)
                                or (x == 2 and z == 6))
    dl = 1 if hasd else 0

    if op == 0xCB:
        if idx:
            d = b(off + 1); d = d - 256 if d & 0x80 else d
            o2 = b(off + 2)
            tgt = '(%s%+d)' % (idx, d)
            L = off + 3
        else:
            o2 = b(1); tgt = None; L = 2
        x2, y2, z2 = o2 >> 6, (o2 >> 3) & 7, o2 & 7
        t = tgt or R8[z2]
        if x2 == 0:
            return L, '%s %s' % (ROT[y2], t)
        return L, '%s %d,%s' % (['', 'bit', 'res', 'set'][x2], y2, t)
    if op == 0xED and not idx:
        o2 = b(1)
        x2, y2, z2 = o2 >> 6, (o2 >> 3) & 7, o2 & 7
        p2, q2 = y2 >> 1, y2 & 1
        if x2 == 1:
            if z2 == 0:
                return 2, 'in %s,(c)' % (R8[y2] if y2 != 6 else 'f')
            if z2 == 1:
                return 2, 'out (c),%s' % (R8[y2] if y2 != 6 else '0')
            if z2 == 2:
                return 2, ('sbc hl,%s' if q2 == 0 else 'adc hl,%s') % R16[p2]
            if z2 == 3:
                nn = b(2) | (b(3) << 8)
                return 4, ('ld ($%04x),%s' % (nn, R16[p2])) if q2 == 0 else ('ld %s,($%04x)' % (R16[p2], nn))
            if z2 == 4:
                return 2, 'neg'
            if z2 == 5:
                return 2, 'retn' if y2 != 1 else 'reti'
            if z2 == 6:
                return 2, 'im %d' % [0, 0, 1, 2, 0, 0, 1, 2][y2]
            return 2, ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld', 'nop', 'nop'][y2]
        if x2 == 2 and z2 <= 3 and y2 >= 4:
            return 2, [['ldi', 'cpi', 'ini', 'outi'], ['ldd', 'cpd', 'ind', 'outd'],
                       ['ldir', 'cpir', 'inir', 'otir'], ['lddr', 'cpdr', 'indr', 'otdr']][y2 - 4][z2]
        return 2, 'db $ed,$%02x' % o2

    if x == 0:
        if z == 0:
            if y == 0: return L, 'nop'
            if y == 1: return L, "ex af,af'"
            d = b(off + 1); d = d - 256 if d & 0x80 else d
            t = (pc + L + 1 + d) & 0xFFFF
            if y == 2: return L + 1, 'djnz $%04x' % t
            if y == 3: return L + 1, 'jr $%04x' % t
            return L + 1, 'jr %s,$%04x' % (CC[y - 4], t)
        if z == 1:
            if q == 0: return L + 2, 'ld %s,$%04x' % (rp(p), w(off + 1))
            return L, 'add %s,%s' % (rp(2), rp(p))
        if z == 2:
            nn = w(off + 1)
            return [(L, 'ld (bc),a'), (L, 'ld a,(bc)'), (L, 'ld (de),a'), (L, 'ld a,(de)'),
                    (L + 2, 'ld ($%04x),%s' % (nn, rp(2))), (L + 2, 'ld %s,($%04x)' % (rp(2), nn)),
                    (L + 2, 'ld ($%04x),a' % nn), (L + 2, 'ld a,($%04x)' % nn)][y]
        if z == 3: return L, ('inc %s' if q == 0 else 'dec %s') % rp(p)
        if z == 4: return L + dl, 'inc %s' % r8(y, off + 1)
        if z == 5: return L + dl, 'dec %s' % r8(y, off + 1)
        if z == 6: return L + dl + 1, 'ld %s,$%02x' % (r8(y, off + 1), b(off + 1 + dl))
        return L, ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf', 'ccf'][y]
    if x == 1:
        if op == 0x76: return L, 'halt'
        if idx and (y == 6 or z == 6):
            a = r8(y, off + 1) if y == 6 else R8[y]
            c = r8(z, off + 1) if z == 6 else R8[z]
            return L + 1, 'ld %s,%s' % (a, c)
        return L, 'ld %s,%s' % (r8(y, off + 1), r8(z, off + 1))
    if x == 2:
        return L + dl, '%s%s' % (ALU[y], r8(z, off + 1))
    # x == 3
    if z == 0: return L, 'ret %s' % CC[y]
    if z == 1:
        if q == 0: return L, 'pop %s' % (idx if (p == 2 and idx) else R16AF[p])
        return L, ['ret', 'exx', 'jp (%s)' % (idx or 'hl'), 'ld sp,%s' % (idx or 'hl')][p]
    if z == 2: return L + 2, 'jp %s,$%04x' % (CC[y], w(off + 1))
    if z == 3:
        if y == 0: return L + 2, 'jp $%04x' % w(off + 1)
        if y == 2: return L + 1, 'out ($%02x),a' % b(off + 1)
        if y == 3: return L + 1, 'in a,($%02x)' % b(off + 1)
        return L, ['', '', '', '', 'ex (sp),%s' % (idx or 'hl'), 'ex de,hl', 'di', 'ei'][y]
    if z == 4: return L + 2, 'call %s,$%04x' % (CC[y], w(off + 1))
    if z == 5:
        if q == 0: return L, 'push %s' % (idx if (p == 2 and idx) else R16AF[p])
        if p == 0: return L + 2, 'call $%04x' % w(off + 1)
        return L, 'db $%02x' % op
    if z == 6: return L + 1, '%s$%02x' % (ALU[y], b(off + 1))
    return L, 'rst $%02x' % (y * 8)


def disasm(m, pc, count):
    out = []
    for _ in range(count):
        n, t = _one(m, pc)
        out.append('%04x:  %-12s %s' % (pc, ' '.join('%02x' % m[(pc + i) & 0xFFFF] for i in range(n)), t))
        pc += n
    return '\n'.join(out), pc


if __name__ == '__main__':
    img = z80_driver_image(ending=len(sys.argv) > 3 and sys.argv[3] == 'ending')
    print(disasm(img, int(sys.argv[1], 0), int(sys.argv[2]))[0])
