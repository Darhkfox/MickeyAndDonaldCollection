r"""Sweep every stage section with the camera (through the game's own camera/streaming code) and check
that the (320+2E)-wide view never shows stale plane tiles.  Writes a report and thumbnail strips.

    python woi_wide_sweep.py [E] [world:entry ...]
"""
import sys
import os
from woi_wide_check import *  # noqa

ENTRIES = {1: range(6), 2: range(19), 3: range(11), 4: range(20), 5: range(19)}


def sweep(world, entry, E=40, planes=('A', 'B'), out=None, patch=None, mode=1):
    md = new_md()
    if patch:
        patch(md)
    goto(md, world, entry, mode=mode)
    md.set_ext(E, sprite_limit=False)
    minx, maxx = md.ram16(CAM_MINX), md.ram16(CAM_MAXX)
    miny, maxy = md.ram16(CAM_MINY), md.ram16(CAM_MAXY)
    pc = PlaneChecker(E, planes=planes)
    ims, lab = [], []

    key0 = (md.ram16(WORLD), md.ram16(ACT))
    info = {'left': None}

    def cb(md):
        if md.ram16(GSTATE) != 3 or (md.ram16(WORLD), md.ram16(ACT)) != key0 or not (md.regs[1] & 0x40):
            if info['left'] is None:
                info['left'] = (md.frame, md.ram16(GSTATE), md.ram16(WORLD), md.ram16(ACT))
            return
        pc.sample(md, xclip=(md.ram16(CAM_MINX), md.ram16(CAM_MAXX) + 320))
        if md.frame % 90 == 0 and len(ims) < 24:
            ims.append(shot(md)); lab.append('%d:%d f%d %s' % (world, entry, md.frame, cam(md)))
    ys = sorted({miny, (miny + maxy) // 2, maxy})
    xs = sorted({minx, (minx + maxx) // 2, maxx})
    # horizontal passes at 3 heights (right then left), vertical passes at 3 x positions
    for y in ys:
        drive_cam(md, minx, y, cb=cb)
        drive_cam(md, maxx, y, cb=cb)
        drive_cam(md, minx, y, cb=cb)
    for x in xs:
        drive_cam(md, x, miny, cb=cb)
        drive_cam(md, x, maxy, cb=cb)
        drive_cam(md, x, miny, cb=cb)
    n, bad = pc.report()
    if out and ims:
        grid(ims, 4, labels=lab, path=out, marks=[E, 320 + E])
    return dict(world=world, entry=entry, act=md.ram16(ACT), bounds=(minx, maxx, miny, maxy),
                n_ext=n, bad=bad, state=md.ram16(GSTATE), left=info['left'])


if __name__ == '__main__':
    E = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    todo = [tuple(map(int, a.split(':'))) for a in sys.argv[2:]] or \
        [(w, e) for w in ENTRIES for e in ENTRIES[w]]
    outdir = os.path.join(SCRATCH, 'sweep')
    os.makedirs(outdir, exist_ok=True)
    from collections import Counter
    for w, e in todo:
        r = sweep(w, e, E, out=os.path.join(outdir, 'sweep_w%d_e%02d.png' % (w, e)))
        c = Counter((b[1][0], b[2]) for b in r['bad'])
        print('W%d E%-2d act=%2d bounds=%s ext_obs=%d bad=%d %s state=%d' % (
            w, e, r['act'], r['bounds'], r['n_ext'], len(r['bad']), sorted(c.items())[:8], r['state']), 'left section:', r['left'], flush=True)
