r"""Static analysis of the object placement lists (RAM $8000 after level load): for every record, the
camera X at which the horizontal spawn trigger fires (original vs. phase-shifted patch) and whether
the object would appear inside the (320+2E) view at that moment (pop-in), for rightward and leftward
camera motion.  Half sprite width assumed = 16 px.

    python woi_wide_spawnstats.py [E]
"""
import sys
from woi_wide_util import *  # noqa
from woi_wide_sweep import ENTRIES


def records(md):
    r = bytes(md.ram)
    base = 0x8000
    off = (r[base] << 8) | r[base + 1]
    a1 = base + 2
    W, H = r[a1], r[a1 + 1]
    a2 = base + off
    out = []
    for cy in range(H):
        for cx in range(W):
            g = r[a1 + 2 + cy * W + cx]
            if not g:
                continue
            p = a2 + ((r[a2 + 2 * (g - 1)] << 8) | r[a2 + 2 * (g - 1) + 1])
            n = r[p] + 1
            p += 1
            for k in range(n):
                rec = r[p:p + 9]
                p += 9
                out.append(dict(cx=cx, cy=cy, type=rec[1], flags=rec[0], y=(rec[5] << 8) | rec[6],
                                x=(rec[7] << 8) | rec[8]))
    return out, W, H


def popin(x, cam, E, hw=16):
    return x + hw > cam - E and x - hw < cam + 320 + E


def main(E):
    seen = set()
    res = {'right_orig': 0, 'right_patch': 0, 'left_orig': 0, 'left_patch': 0, 'n': 0}
    per = []
    for w in ENTRIES:
        for e in ENTRIES[w]:
            md = new_md()
            goto(md, w, e, settle=5)
            key = (w, md.ram16(ACT))
            if key in seen:
                continue
            seen.add(key)
            recs, W, H = records(md)
            for r in recs:
                cx, x = r['cx'], r['x']
                if cx < 4 or cx >= W - 1:      # spawned by the initial spawn / never by a trigger
                    continue
                res['n'] += 1
                a = popin(x, 128 * (cx - 3), E)
                b = popin(x, 128 * (cx - 3) - E, E)
                c = popin(x, 128 * (cx + 1) - 1, E)
                d = popin(x, 128 * (cx + 1) - 1 + E, E)
                res['right_orig'] += a; res['right_patch'] += b
                res['left_orig'] += c; res['left_patch'] += d
                if b or d:
                    per.append((w, key[1], r['type'], cx, x, x - 128 * cx, 'R' if b else '', 'L' if d else ''))
    print('E=%d  trigger-spawned records=%d' % (E, res['n']))
    print('  pop-in inside the wide view, moving right: original %d, patched %d' % (res['right_orig'], res['right_patch']))
    print('  pop-in inside the wide view, moving left : original %d, patched %d' % (res['left_orig'], res['left_patch']))
    for p in per:
        print('   patched pop-in: W%d act%d type %d cell %d x=%d (dx=%d) %s%s' % p)


if __name__ == '__main__':
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 40)
