"""Reproduces the verification numbers of docs/re_coi_wide.md.

    python tools/re/coi_wide_verify.py planes [E] [clamp]     # plane validator over every round (sec. 2/4)
    python tools/re/coi_wide_verify.py objects [E] [rounds…]  # object pop-in comparison (sec. 3)
    python tools/re/coi_wide_verify.py budget                  # sprite piece budget (sec. 7)

The camera is driven directly (test-only NOP of 'jsr $F71A' at $F6E4, see coi_wide_common.camera_driver);
the player is parked at the view centre with invulnerability so the round keeps running.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coi_wide_common as W  # noqa: E402


def serpentine(c, step=112):
    ys = list(range(c['miny'], c['maxy'] + 1, step))
    if ys[-1] != c['maxy']:
        ys.append(c['maxy'])
    pts = []
    for i, y in enumerate(ys):
        pts += [(c['minx'], y), (c['maxx'], y)] if i % 2 == 0 else [(c['maxx'], y), (c['minx'], y)]
    return pts


class _Stop(Exception):
    pass


def planes(E=40, clamp=False, patched=True, speed=6, rounds=range(0x1C)):
    """Frames with wrong name-table cells in the extensions/centre and frames showing cells outside the map."""
    md = W.new(); md.set_ext(E, sprite_limit=False)
    if patched:
        W.apply_patches(md, E, clamp=clamp)
    base = md.save()
    for n in rounds:
        md.load(base)
        W.camera_driver(md, False)
        W.goto_level(md, n)
        W.camera_driver(md, True)
        c = W.cam(md)
        st = dict(frames=0, ext_bad=0, centre_bad=0, outsideA=0, outsideB=0)

        def cb(md, step):
            if md.ram16(W.MODE) != 0x0E:
                raise _Stop()
            r = W.check_planes(md, E)
            st['frames'] += 1
            st['ext_bad'] += any(v['left'] + v['right'] for v in r.values())
            st['centre_bad'] += any(v['center'] for v in r.values())
            st['outsideA'] += r['A']['outside'] > 0
            st['outsideB'] += 'B' in r and r['B']['outside'] > 0
        try:
            W.cam_path(md, serpentine(c), speed=speed, every=1, cb=cb)
        except _Stop:
            st['ended'] = True
        W.camera_driver(md, False)
        print('%02X %-28s camX %5d..%-5d %s' % (n, W.ROUND_NAMES[n], c['minx'], c['maxx'], st), flush=True)


def spawns(n, E, patched, speed=3):
    md = W.new()
    if patched:
        W.apply_patches(md, E)
    md.set_ext(E, sprite_limit=False)
    W.goto_level(md, n)
    W.camera_driver(md, True)
    c = W.cam(md)
    snap = lambda: {s: (md.ram16(s), W.s16(md.ram16(s + 0x10)), W.s16(md.ram16(s + 0x14)), md.ram16(s + 0x24))
                    for s in W.OBJ_SLOTS if md.ram16(s)}
    prev = [snap()]; out = []

    def cb(md, step):
        cc = W.cam(md); cur = snap()
        for s, v in cur.items():
            if v[3] and (s not in prev[0] or prev[0][s][3] != v[3]):
                out.append((step, v[0], v[1] - cc['x'], v[2] - cc['y']))
        prev[0] = cur
    W.cam_path(md, [(c['maxx'], c['y']), (c['minx'], c['y']), (c['maxx'], c['y'])], speed=speed, every=1, cb=cb)
    return out


def objects(E=40, rounds=(0x00, 0x02, 0x06, 0x09, 0x0E, 0x15, 0x17, 0x18, 0x19)):
    inside = lambda ev, lo, hi: [e for e in ev if lo + 8 <= e[2] < hi - 8 and 0 <= e[3] < 192]
    for n in rounds:
        so = spawns(n, E, False); sp = spawns(n, E, True)
        print('%02X %-24s spawns inside: orig 320-view %d | unpatched %d-view %d | patched %d-view %d' % (
            n, W.ROUND_NAMES[n], len(inside(so, 0, 320)), 320 + 2 * E, len(inside(so, -E, 320 + E)),
            320 + 2 * E, len(inside(sp, -E, 320 + E))), flush=True)


def budget(E=40, rounds=(0x09, 0x17, 0x15, 0x00, 0x06)):
    for n in rounds:
        res = []
        for patched in (False, True):
            md = W.new()
            if patched:
                W.apply_patches(md, E, clamp=True)
            W.goto_level(md, n); W.camera_driver(md, True); c = W.cam(md)
            m = [99]
            W.cam_path(md, [(c['maxx'], c['y']), (c['minx'], c['y'])], speed=3, every=1,
                       cb=lambda md, s: m.__setitem__(0, min(m[0], md.ram8(0xFFF402))))
            res.append(m[0])
        print('%02X %-24s min remaining sprite pieces of 73: orig %d, patched %d' % (n, W.ROUND_NAMES[n], res[0], res[1]))


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'planes'
    E = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    if what == 'planes':
        planes(E, clamp=len(sys.argv) > 3 and sys.argv[3] == 'clamp')
    elif what == 'objects':
        objects(E, [int(a, 16) for a in sys.argv[3:]] or (0x00, 0x02, 0x06, 0x09, 0x0E, 0x15, 0x17, 0x18, 0x19))
    elif what == 'budget':
        budget(E)
