r"""Automated widescreen checks for QuackShot (see docs/re_qs_wide.md).

    python tools/re/qs_wide_verify.py stream [--orig] [--clamp] [--E 40] [--areas 2.0,3.1]
        pan the camera over every area (R, D, L, U, diagonals) and compare the plane A/B
        nametables with the map for the whole 320+2E view every frame.
    python tools/re/qs_wide_verify.py spawn [--orig]
        pan right then left through object-rich areas and report objects that are spawned
        while already inside the visible 320+2E view (pop-in).
    python tools/re/qs_wide_verify.py sprites [--orig]
        walk right in several areas and count sprite pieces drawn in the extension columns.
    python tools/re/qs_wide_verify.py edges [--clamp]
        report camera X range reached and the mask rule result at both ends of each area.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qs_wide_util import *  # noqa
import qs_wide_patch as P

AREAS = {0: 1, 1: 3, 2: 4, 3: 7, 4: 5, 5: 4, 6: 9, 7: 6, 8: 5, 9: 4}


def all_areas():
    return [(l, a) for l, n in AREAS.items() for a in range(n)]


def base_state():
    b = MD(QS)
    load_state(b, 'qs_duckburg.state')
    return b.save()


def make(st, args, loc, area, hack=True):
    md = MD(QS)
    if not args.orig:
        P.apply(md, args.E, camera_clamp=args.clamp)
    if hack:
        test_camera_hack(md)
    md.load(st)
    goto_area(md, loc, area)
    md.set_ext(args.E, sprite_limit=False)
    return md


def mask_rule(md, E):
    """Renderer rule (doc section 4): columns of the extension outside plane A's camera range."""
    X = md.ram16(0xFF8524)
    xmin = md.ram16(0xFF8504)
    xmax = md.ram16(0xFF8500)
    left = max(0, min(E, E - (X - xmin)))
    right = max(0, min(E, X + E - xmax))
    return left, right


def cmd_stream(args):
    st = base_state()
    E = args.E
    total = 0
    for loc, ar in args.areas:
        md = make(st, args, loc, ar)
        log = []

        def cb(f, tag):
            if (md.ram16(STATE) & 0x7FFF) != 5:
                return
            LA = layer(md, LAYER_A); LB = layer(md, LAYER_B)
            a = check_plane(md, LAYER_A, 0xC000, E, xlim=(0, LA['xmax'] + 320), ylim=(0, LA['ymax'] + 224), displayed='both')
            b = check_plane(md, LAYER_B, 0xE000, E, xlim=(0, LB['xmax'] + 320), ylim=(0, LB['ymax'] + 224), displayed='both')
            if a or b:
                log.append((tag, f, LA['x'], LA['y'], a[:4], b[:4]))
        L = layer(md, LAYER_A)
        x0 = L['x']
        pan(md, 4, 0, (L['xmax'] - L['x']) // 4 + 10, lambda f: cb(f, 'R'))
        x1 = md.ram16(0xFF8524)
        pan(md, 0, 3, (L['ymax'] - L['y']) // 3 + 10, lambda f: cb(f, 'D'))
        pan(md, -4, 0, L['xmax'] // 4 + 10, lambda f: cb(f, 'L'))
        x2 = md.ram16(0xFF8524)
        pan(md, 0, -3, L['ymax'] // 3 + 10, lambda f: cb(f, 'U'))
        pan(md, 4, 2, 200, lambda f: cb(f, 'RD'))
        pan(md, -3, -1.5, 100, lambda f: cb(f, 'LU'))
        total += len(log)
        print('loc %d area %d: Xrange %d..%d (start %d, xmax %d)  bad frames %d %s'
              % (loc, ar, x2, x1, x0, L['xmax'], len(log), log[:2]), flush=True)
    print('TOTAL bad frames', total)


def slots(md):
    r = {}
    for i in range(48):
        a = 0xFF9000 + i * 64
        if md.ram16(a):
            r[i] = (md.ram16(a), md.ram16(a + 0x10), md.ram16(a + 0x14), md.ram16(a + 0x2E))
    return r


def cmd_spawn(args):
    st = base_state()
    E = args.E
    areas = args.areas if args.areas_given else [(2, 0), (3, 0), (7, 0), (6, 0), (9, 0), (5, 0), (8, 2)]
    for loc, ar in areas:
        md = make(st, args, loc, ar)
        ev = []; nmax = [0]; prev = [slots(md)]

        def cb(f, d):
            s = slots(md)
            X = md.ram16(0xFF8524); Y = md.ram16(0xFF8520)
            nmax[0] = max(nmax[0], len(s))
            for i, (t, x, y, sp) in s.items():
                if i not in prev[0] or prev[0][i][0] != t:
                    sx = x - X; sy = y - Y
                    if sp and -E - 8 <= sx < 320 + E + 8 and -16 <= sy < 200:
                        ev.append((d, f, t, sx, sy))
            prev[0] = s
        L = layer(md, LAYER_A)
        pan(md, 3, 0, (L['xmax'] - L['x']) // 3, lambda f: cb(f, 'R'))
        pan(md, -3, 0, L['xmax'] // 3, lambda f: cb(f, 'L'))
        print('loc %d area %d: spawned inside view: R %d, L %d; max level slots %d  %s'
              % (loc, ar, sum(e[0] == 'R' for e in ev), sum(e[0] == 'L' for e in ev), nmax[0], ev[:4]), flush=True)


def cmd_sprites(args):
    st = base_state()
    E = args.E
    for loc, ar in ((2, 0), (3, 0), (7, 0), (6, 0)):
        md = make(st, args, loc, ar, hack=False)
        rx = fr = 0
        for f in range(500):
            md.hold('RIGHT+C' if f % 40 < 8 else 'RIGHT')
            md.run(1)
            ps = [p for p in sat_pieces(md)[4:] if -32 < p[2] < 192]
            r = [p for p in ps if 320 <= p[1] < 320 + E]
            rx += len(r); fr += bool(r)
        print('loc %d area %d: pieces starting in right extension: %d (in %d frames)' % (loc, ar, rx, fr), flush=True)


def cmd_edges(args):
    st = base_state()
    E = args.E
    for loc, ar in args.areas:
        md = make(st, args, loc, ar)
        L = layer(md, LAYER_A)
        m0 = mask_rule(md, E); x0 = L['x']
        pan(md, 4, 0, (L['xmax'] - L['x']) // 4 + 10)
        m1 = mask_rule(md, E); x1 = md.ram16(0xFF8524)
        pan(md, -4, 0, L['xmax'] // 4 + 10)
        m2 = mask_rule(md, E); x2 = md.ram16(0xFF8524)
        print('loc %d area %d: xmin %d xmax %d | start X %d mask %s | right end X %d mask %s | left end X %d mask %s'
              % (loc, ar, L['xmin'], L['xmax'], x0, m0, x1, m1, x2, m2), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['stream', 'spawn', 'sprites', 'edges'])
    ap.add_argument('--orig', action='store_true', help='no patches')
    ap.add_argument('--clamp', action='store_true', help='include optional camera clamp patch')
    ap.add_argument('--E', type=int, default=40)
    ap.add_argument('--areas', default='')
    args = ap.parse_args()
    args.areas_given = bool(args.areas)
    args.areas = [tuple(int(v) for v in a.split('.')) for a in args.areas.split(',')] if args.areas else all_areas()
    globals()['cmd_' + args.cmd](args)


if __name__ == '__main__':
    main()
