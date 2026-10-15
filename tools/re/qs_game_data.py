"""Static ROM data helpers for QuackShot (REV00): Kosinski decompression (trap #4),
level entry-point table ($7EE26 -> RAM $FFA400), text decoding.

    python qs_game_data.py entries     # dump every location's entry-point records
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from m68dis import rom  # noqa: E402

R = rom('qs')


def u16(b, a):
    return (b[a] << 8) | b[a + 1]


def s16(b, a):
    v = u16(b, a)
    return v - 0x10000 if v & 0x8000 else v


def kosinski(src):
    """Standard Sega Kosinski decompressor (same format as ROM $24A6, trap #4)."""
    out = bytearray()
    pos = src + 2
    bits = R[src] | (R[src + 1] << 8)
    nbits = 16

    def getbit():
        # the 68k code reloads the 16-bit descriptor immediately after its last bit is used
        nonlocal bits, nbits, pos
        b = bits & 1
        bits >>= 1
        nbits -= 1
        if nbits == 0:
            bits = R[pos] | (R[pos + 1] << 8)
            pos += 2
            nbits = 16
        return b

    while True:
        if getbit():
            out.append(R[pos]); pos += 1
            continue
        if getbit():
            lo = R[pos]; hi = R[pos + 1]; pos += 2
            off = ((hi & 0xF8) << 5) | lo
            off = off - 0x2000
            cnt = hi & 7
            if cnt == 0:
                c = R[pos]; pos += 1
                if c == 0:
                    break
                if c == 1:
                    continue
                cnt = c + 1
            else:
                cnt += 2
        else:
            cnt = (getbit() << 1) | getbit()
            cnt += 2
            off = R[pos] - 0x100; pos += 1
        for _ in range(cnt):
            out.append(out[len(out) + off])
    return bytes(out)


def trap4_entry(i):
    """(rom_src, ram_dst) of trap #4 resource i."""
    return struct.unpack('>II', R[0x1D3E + i * 8:0x1D3E + i * 8 + 8])


# ---- text (map/HUD font): '0'-'9' = $01-$0A, 'A'-'N' = $0B-$18, 'P'-'Z' = $19-$23 (no 'O': uses '0'),
#      $2A = '-', $3B = space
def dec_char(c):
    if 1 <= c <= 10:
        return 'O' if c == 1 else str(c - 1)
    if 0x0B <= c <= 0x18:
        return chr(ord('A') + c - 0x0B)
    if 0x19 <= c <= 0x23:
        return chr(ord('P') + c - 0x19)
    return {0x2A: '-', 0x3B: ' '}.get(c, '?')


LOCATION_NAMES = {}
for _i in range(11):
    _b = R[0x4DA8 + _i * 24:0x4DA8 + _i * 24 + 24]
    LOCATION_NAMES[_i + 1] = ''.join(dec_char(_b[j + 1]) for j in range(0, 24, 2)).strip()


def debug_stage_table():
    """The unused debug stage list at $9DA6/$9DD6/$A06C: 48 entries (stage -> location, entry, label)."""
    out = []
    for i in range(48):
        lab = R[0xA06C + i * 3:0xA06C + i * 3 + 3]
        label = ''.join(dec_char(c) if c != 1 else '0' for c in lab)
        out.append((i, R[0x9DA6 + i], R[0x9DD6 + i], label))
    return out


def level_header():
    """Decompressed trap#4 resource $4C (ROM $7EE26 -> RAM $FFA400)."""
    return kosinski(0x7EE26)


def entry_records():
    """{location: [ (entry, dict), ...]} parsed exactly like $3EF8 does."""
    H = level_header()
    base = u16(H, 0)                         # $A400: offset to per-location table
    res = {}
    starts = sorted(set(base + u16(H, base + 2 * loc) for loc in range(1, 10)))
    for loc in range(1, 10):          # 10/11 (ROOM1/ROOM2) point into other tables: not real locations
        t = base + u16(H, base + 2 * loc)
        later = [s for s in starts if s > t]
        if later:
            n = (later[0] - t) // 2
        else:                         # last table: offsets until a 0 word
            n = 0
            while u16(H, t + 2 * n):
                n += 1
        recs = []
        for e in range(n):
            a = t + u16(H, t + 2 * e)
            rec = dict(room=u16(H, a), bgA_x=u16(H, a + 2), bgA_y=u16(H, a + 4), bgB_x=u16(H, a + 6),
                       bgB_y=u16(H, a + 8), a2d4=u16(H, a + 10), a2d0=u16(H, a + 12),
                       b8f3a=H[a + 14], music=H[a + 15])
            recs.append((e, rec))
        res[loc] = recs
    return res


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'entries':
        H = level_header()
        print('header size', hex(len(H)))
        for loc, recs in entry_records().items():
            print('%2d %s' % (loc, LOCATION_NAMES.get(loc)))
            for e, r in recs:
                print('    entry %2d room %2d camA=(%d,%d) camB=(%d,%d) a2d4=%d a2d0=%d b=%d music=$%02X' % (
                    e, r['room'], r['bgA_x'], r['bgA_y'], r['bgB_x'], r['bgB_y'], r['a2d4'], r['a2d0'],
                    r['b8f3a'], r['music']))
    elif len(sys.argv) > 1 and sys.argv[1] == 'stages':
        for i, loc, ent, lab in debug_stage_table():
            print('$%02X  %-4s loc %d (%s) entry %d' % (i, lab, loc, LOCATION_NAMES.get(loc, '-'), ent))


# ---- dialogue / LOOK messages: trap#4 resource $4E (ROM $7BF00, English) -> RAM $FFCE00
#      message id d1 is written to $FF8FBC (routine $1006/$1012), shown by $1018.
_MSG_SPECIAL = {0x24: '?', 0x25: '!', 0x26: '.', 0x27: ',', 0x28: '"', 0x29: '"', 0x2A: '-', 0x2B: ':',
                0x2C: '\'', 0x2D: '\'', 0x3B: ' ', 0xFF: '/'}


def _mchar(c):
    if 1 <= c <= 10:
        return 'O' if c == 1 else str(c - 1)
    if 0x0B <= c <= 0x18:
        return chr(ord('A') + c - 0x0B)
    if 0x19 <= c <= 0x23:
        return chr(ord('P') + c - 0x19)
    return _MSG_SPECIAL.get(c, '<%02x>' % c)


def messages(src=0x7BF00):
    d = kosinski(src)
    base = u16(d, 0)                 # $CE00: offset of message table ($CE04)
    n = (u16(d, 2) - base) // 4      # $CE02: offset of the next table (speaker/portrait data)
    out = []
    for i in range(n):
        style = u16(d, base + 4 * i)
        a = base + u16(d, base + 4 * i + 2)
        cnt = u16(d, a)
        pages = []
        for k in range(cnt + 1):
            p = int.from_bytes(d[a + 2 + 4 * k:a + 6 + 4 * k], 'big') - 0xFFCE00
            if not (0 <= p < len(d)):
                break
            s = ''
            j = p + 1
            while j < len(d) and d[j] not in (0x38,) and j < p + 200:
                s += _mchar(d[j]); j += 1
            s = s.replace('/', ' ').replace('   ', ' ')
            pages.append((d[p], ' '.join(s.split())))
        out.append((i, style, pages))
    return out
