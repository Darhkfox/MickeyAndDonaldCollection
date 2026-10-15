"""68000 disassembly helpers (capstone) for the two ROMs.

    python m68dis.py coi 0x200 40          # disassemble 40 instructions at $200
    python m68dis.py qs 0x52a 30
    python m68dis.py coi xref 0xFFFF8E      # find absolute references to an address
    python m68dis.py coi calls 0x1234       # find JSR/BSR/JMP to an address

From Python:
    from m68dis import rom, dis, xrefs
"""
import sys
import struct
import os
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMS = {
    'coi': os.path.join(ROOT, 'Mickey Mouse - Castle of Illusion (U) [!].bin'),
    'qs': os.path.join(ROOT, 'Quack Shot Starring Donald Duck (JUE) (REV 00) [!].bin'),
    'woi': os.path.join(ROOT, 'World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin'),
}
_cache = {}


def rom(game):
    if game not in _cache:
        _cache[game] = open(ROMS[game], 'rb').read()
    return _cache[game]


_cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)


def insns(game, addr, count=30):
    data = rom(game)
    out = []
    pos = addr
    while len(out) < count and pos < len(data):
        got = False
        for i in _cs.disasm(data[pos:pos + 16], pos, 1):
            out.append((i.address, i.size, i.mnemonic, i.op_str, data[pos:pos + i.size]))
            pos += i.size
            got = True
        if not got:
            w = struct.unpack('>H', data[pos:pos + 2])[0]
            out.append((pos, 2, 'dc.w', '$%04x' % w, data[pos:pos + 2]))
            pos += 2
    return out


def dis(game, addr, count=30, show_bytes=False):
    lines = []
    for a, sz, m, ops, b in insns(game, addr, count):
        hx = (' ' + b.hex()).ljust(22) if show_bytes else ''
        lines.append('%06x:%s  %-8s %s' % (a, hx, m, ops))
    return '\n'.join(lines)


def u16(game, a):
    return struct.unpack('>H', rom(game)[a:a + 2])[0]


def u32(game, a):
    return struct.unpack('>I', rom(game)[a:a + 4])[0]


def xrefs(game, target):
    """Find instructions whose encoding contains target as abs.w (for $FFxxxx RAM) or abs.l."""
    data = rom(game)
    hits = set()
    t = target & 0xFFFFFF
    pats = [struct.pack('>I', t), struct.pack('>I', t | 0xFF000000)]
    if t >= 0xFF8000:
        pats.append(struct.pack('>H', t & 0xFFFF))
    for p in pats:
        start = 0
        while True:
            i = data.find(p, start)
            if i < 0:
                break
            start = i + 1
            if i & 1:
                continue
            # try instruction starts up to 8 bytes before
            for back in range(2, 10, 2):
                s = i - back
                if s < 0:
                    continue
                ins = list(_cs.disasm(data[s:s + 16], s, 1))
                if ins and s + ins[0].size > i:
                    hits.add((s, ins[0].mnemonic + ' ' + ins[0].op_str))
                    break
    return sorted(hits)


def calls(game, target):
    """Find JSR/JMP abs and BSR/BRA/Bcc/JSR (PC) referencing target."""
    data = rom(game)
    res = []
    for pos in range(0, len(data) - 6, 2):
        w = struct.unpack('>H', data[pos:pos + 2])[0]
        if w in (0x4EB9, 0x4EF9) and u32(game, pos + 2) == target:
            res.append((pos, 'jsr' if w == 0x4EB9 else 'jmp'))
        elif w in (0x4EB8, 0x4EF8) and (struct.unpack('>h', data[pos + 2:pos + 4])[0] & 0xFFFFFF) == target:
            res.append((pos, 'jsr.w' if w == 0x4EB8 else 'jmp.w'))
        elif w in (0x4EBA, 0x4EFA) and pos + 2 + struct.unpack('>h', data[pos + 2:pos + 4])[0] == target:
            res.append((pos, 'jsr(pc)' if w == 0x4EBA else 'jmp(pc)'))
        elif (w & 0xF000) == 0x6000:
            d8 = w & 0xFF
            if d8 == 0:
                disp = struct.unpack('>h', data[pos + 2:pos + 4])[0]
            else:
                disp = d8 - 256 if d8 & 0x80 else d8
            if pos + 2 + disp == target:
                cc = (w >> 8) & 15
                res.append((pos, ['bra', 'bsr', 'bhi', 'bls', 'bcc', 'bcs', 'bne', 'beq', 'bvc', 'bvs', 'bpl', 'bmi', 'bge', 'blt', 'bgt', 'ble'][cc]))
    return res


if __name__ == '__main__':
    g = sys.argv[1]
    if sys.argv[2] == 'xref':
        for a, t in xrefs(g, int(sys.argv[3], 0)):
            print('%06x  %s' % (a, t))
    elif sys.argv[2] == 'calls':
        for a, t in calls(g, int(sys.argv[3], 0)):
            print('%06x  %s' % (a, t))
    else:
        print(dis(g, int(sys.argv[2], 0), int(sys.argv[3]) if len(sys.argv) > 3 else 30, show_bytes=True))
