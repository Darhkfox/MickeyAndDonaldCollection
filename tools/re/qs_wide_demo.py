r"""QuackShot widescreen demo: before/after screenshot strips at E=40 (see docs/re_qs_wide.md).

    python tools/re/qs_wide_demo.py [outdir] [--E 40] [--clamp]

For each section the same input script is played twice from the same state:
  BEFORE = original ROM rendered 320+2E wide (what the core shows without any patch),
  AFTER  = patched ROM (qs_wide_patch) + the port-side rules from the doc: HUD remap for
           lines 192..223 (section 5), edge mask (section 4a), pillarbox outside gameplay (6).
Red dots mark the original 320-px view. Output: <outdir>/qs_wide_<section>.png (rows of
frames, BEFORE above AFTER) and a summary of the automatic nametable check per frame.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qs_wide_util import *  # noqa
import qs_wide_patch as P


def gameplay(md):
    return (md.ram16(STATE) & 0x7FFF) == 5 and md.ram16(VMODE) == 1


def port_view(md, E):
    """Screenshot post-processed with the port rules (mask, HUD remap, pillarbox)."""
    im = shot(md)
    W = im.size[0]
    d = ImageDraw.Draw(im)
    if not gameplay(md):
        d.rectangle([0, 0, E - 1, 223], fill=(0, 0, 0))
        d.rectangle([W - E, 0, W - 1, 223], fill=(0, 0, 0))
        return im
    X = md.ram16(0xFF8FB4)
    xmin = md.ram16(0xFF8504); xmax = md.ram16(0xFF8500)
    ml = max(0, min(E, E - (X - xmin)))
    mr = max(0, min(E, X + E - xmax))
    if ml:
        d.rectangle([0, 0, ml - 1, 191], fill=(0, 0, 0))
    if mr:
        d.rectangle([W - mr, 0, W - 1, 191], fill=(0, 0, 0))
    if md.regs[18] == 0x98:
        src = im.copy().load(); dst = im.load()
        for xo in range(W):
            xs = E + hud_src_x(xo, E)
            for y in range(192, 224):
                dst[xo, y] = src[xs, y]
    return im


# Each section: (name, loc, area, list of steps).  A step is (frames, buttons) for play or
# ('pan', vx, vy, frames) for a test-only camera pan (vertical shafts), or ('shot',).
SECTIONS = [
    ('duckburg_walk', 1, 0, [(40, 'RIGHT'), ('shot',), (90, 'RIGHT'), ('shot',), (120, 'RIGHT'), ('shot',),
                             (80, 'LEFT'), ('shot',), (80, 'LEFT'), ('shot',)]),
    ('mexico_bands', 2, 0, [(1, ''), ('shot',), (100, 'RIGHT'), ('shot',), (150, 'RIGHT+C'), ('shot',),
                            (150, 'RIGHT'), ('shot',)]),
    ('egypt_sprites', 7, 0, [(60, 'RIGHT'), ('shot',), (90, 'RIGHT'), ('shot',), (150, 'RIGHT'), ('shot',)]),
    ('india_sprites', 6, 0, [(200, 'RIGHT'), ('shot',), (130, 'RIGHT'), ('shot',), (90, 'RIGHT'), ('shot',)]),
    ('transyl_walk_left', 3, 0, [('pan', 4, 0, 250), ('shot',), ('pan', -3, 0, 60), ('shot',),
                                 ('pan', -3, 0, 60), ('shot',), ('pan', -3, 0, 60), ('shot',)]),
    ('southpole_shaft', 5, 3, [('pan', 0, 3, 60), ('shot',), ('pan', 2, 3, 80), ('shot',), ('pan', 0, -3, 90), ('shot',)]),
    ('egypt_shaft', 7, 1, [('pan', 0, 3, 90), ('shot',), ('pan', 3, 2, 90), ('shot',), ('pan', -2, -3, 90), ('shot',)]),
    ('viking_ship', 4, 0, [(60, 'RIGHT'), ('shot',), (120, 'RIGHT'), ('shot',), (120, 'RIGHT'), ('shot',)]),
    ('southpole_ice', 5, 2, [(80, 'RIGHT'), ('shot',), (150, 'RIGHT'), ('shot',), (150, 'RIGHT'), ('shot',)]),
    ('room_512', 6, 1, [(1, ''), ('shot',), (100, 'RIGHT'), ('shot',), (100, 'LEFT'), ('shot',)]),
    ('room_320', 6, 7, [(1, ''), ('shot',), (100, 'RIGHT'), ('shot',)]),
    ('start_menu', 1, 0, [(60, 'RIGHT'), ('shot',), (4, 'START'), (40, ''), ('shot',), (4, 'START'), (60, ''), ('shot',)]),
]


def run_section(st, sec, E, patched, clamp):
    name, loc, area, steps = sec
    md = MD(QS)
    if patched:
        P.apply(md, E, camera_clamp=clamp)
    if any(x[0] == 'pan' for x in steps):
        test_camera_hack(md)         # test-only: persistent camera velocity (pan sections only)
    md.load(st)
    goto_area(md, loc, area, 150)
    md.set_ext(E, sprite_limit=False)
    shots = []; checks = []
    for s in steps:
        if s[0] == 'shot':
            shots.append((port_view(md, E) if patched else mark_view(shot(md), E),
                          'X=%d Y=%d' % (md.ram16(0xFF8524), md.ram16(0xFF8520))))
            if gameplay(md):
                LA = layer(md, LAYER_A); LB = layer(md, LAYER_B)
                a = check_plane(md, LAYER_A, 0xC000, E, xlim=(0, LA['xmax'] + 320), ylim=(0, LA['ymax'] + 224), displayed='both')
                b = check_plane(md, LAYER_B, 0xE000, E, xlim=(0, LB['xmax'] + 320), ylim=(0, LB['ymax'] + 224), displayed='both')
                checks.append(len(a) + len(b))
        elif s[0] == 'pan':
            pan(md, s[1], s[2], s[3])
        else:
            frames, btn = s
            md.hold(btn if btn else 0)
            md.run(frames)
            md.hold(0)
    if patched:
        shots = [(mark_view(im, E), lab) for im, lab in shots]
    return shots, checks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir', nargs='?', default=os.path.join(ROOT, 'build', 'qs_wide_demo'))
    ap.add_argument('--E', type=int, default=40)
    ap.add_argument('--clamp', action='store_true', help='also apply the optional camera clamp patch')
    ap.add_argument('--only', default='')
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    b = MD(QS); load_state(b, 'qs_duckburg.state'); st = b.save()
    for sec in SECTIONS:
        if args.only and sec[0] not in args.only.split(','):
            continue
        before, cb = run_section(st, sec, args.E, False, False)
        after, ca = run_section(st, sec, args.E, True, args.clamp)
        ims = [i for i, _ in before] + [i for i, _ in after]
        labs = ['BEFORE ' + l + ' bad cells %s' % (cb[k] if k < len(cb) else '-') for k, (_, l) in enumerate(before)] + \
               ['AFTER ' + l + ' bad cells %s' % (ca[k] if k < len(ca) else '-') for k, (_, l) in enumerate(after)]
        path = os.path.join(args.outdir, 'qs_wide_%s.png' % sec[0])
        grid(ims, len(before), labs, path)
        print('%-18s before: bad cells per shot %s | after: %s -> %s' % (sec[0], cb, ca, path), flush=True)


if __name__ == '__main__':
    main()
