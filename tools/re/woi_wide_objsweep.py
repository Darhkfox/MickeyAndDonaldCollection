r"""Object pop-in sweep: drive the camera through every section (right, left, right) and count objects
that spawn / despawn / un-hide (after being hidden >= 12 frames) at least 8 px inside the visible view:
  base  = original ROM, original 320-px view (E=0): pops that exist in the original game
  orig  = original ROM seen through the (320+2E) view
  patch = patched ROM, (320+2E) view

    python woi_wide_objsweep.py [E] [world:entry ...]
"""
import sys
from woi_wide_check import *  # noqa
import woi_wide_patch as P
from woi_wide_sweep import ENTRIES


def run(world, entry, E, patched, view=None):
    md = new_md()
    if patched:
        P.apply(md, E)
    E = E if view is None else view
    goto(md, world, entry)
    md.set_ext(E, sprite_limit=False)
    sc = SpriteChecker(E)
    key0 = (md.ram16(WORLD), md.ram16(ACT))
    minx, maxx = md.ram16(CAM_MINX), md.ram16(CAM_MAXX)
    miny, maxy = md.ram16(CAM_MINY), md.ram16(CAM_MAXY)

    def cb(md):
        if md.ram16(GSTATE) == 3 and (md.ram16(WORLD), md.ram16(ACT)) == key0:
            sc.sample(md)
    for y in sorted({miny, maxy}):
        for tx in (maxx, minx, maxx):
            drive_cam(md, tx, y, cb=cb)
    ext = [e for e in sc.events if not (0 <= e[4] < 320)]
    return sc.events, ext


if __name__ == '__main__':
    E = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    todo = [tuple(map(int, a.split(':'))) for a in sys.argv[2:]] or         [(w, e) for w in ENTRIES for e in ENTRIES[w]]
    tot = [0, 0, 0]
    for w, e in todo:
        base, _ = run(w, e, E, False, view=0)
        u, _ = run(w, e, E, False)
        p, _ = run(w, e, E, True)
        # items (type 216) collected by the teleported player fly off and delete themselves: ignore
        base, u, p = [[ev for ev in x if ev[3] != 216] for x in (base, u, p)]
        tot = [tot[0] + len(base), tot[1] + len(u), tot[2] + len(p)]
        print('W%d:%-2d  pops: base=%d orig=%d patch=%d' % (w, e, len(base), len(u), len(p)), flush=True)
        for ev in p:
            print('     patch: f%d %s slot=%06X type=%d relx=%d rely=%d' % ev, flush=True)
    print('TOTAL base=%d orig=%d patch=%d' % tuple(tot))
