r"""World of Illusion widescreen demo: plays several sections at E=40 twice - original ROM ("before")
and with the woi_wide_patch patches + the level-edge mask rule ("after") - and writes side-by-side
screenshot strips (top row before, bottom row after) to the output folder.

    python woi_wide_demo.py [outdir] [E]

Magenta ticks mark the original 320-px view.  In the "after" rows the level-edge mask
(doc section 4) is drawn red-tinted so it can be reviewed (the port draws it black).
"""
import os
import sys
from woi_wide_util import *  # noqa
import woi_wide_patch as P

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRATCH, 'demo')
E = int(sys.argv[2]) if len(sys.argv) > 2 else 40


def scenario(name, world, entry, mode, script, patched):
    md = new_md()
    if patched:
        P.apply(md, E)
    goto(md, world, entry, mode=mode)
    md.set_ext(E, sprite_limit=False)
    md.run(1)                      # render one frame at the new width
    ims, lab = [], []

    def grab(tag=''):
        im = shot_masked(md, E) if patched else shot(md)
        ims.append(im)
        ml, mr = mask_rule(md, E)
        lab.append('%s %s f%d cam=%s mask=%d/%d %s' % ('AFTER' if patched else 'BEFORE', name,
                                                       md.frame, cam(md), ml, mr, tag))
    script(md, grab)
    md.hold(0); md.hold2(0)
    return ims, lab


# ---------------------------------------------------------------- scripts
def s_forest(md, grab):
    """W1 act 0: walk right with jumps: enemies enter from the right extension."""
    for k in range(8):
        md.hold('RIGHT'); md.run(40)
        md.hold('RIGHT+C'); md.run(8)
        md.hold('RIGHT'); md.run(12)
        grab()


def s_left(md, grab):
    """W1 act 3: go right, then walk back left: objects must already exist in the left extension."""
    drive_cam(md, 700, md.ram16(CAMY))
    y = md.ram16(CAMY)
    for tx in range(640, 0, -80):
        drive_cam(md, tx, y)
        grab('drive-left')


def s_2p(md, grab):
    """W1 act 0, 2 players: walk right together, then spread apart (players may use the whole width).
    Hearts are refilled every frame so nobody blinks/dies during the demo."""
    def run(b1, b2, n):
        md.hold(b1); md.hold2(b2)
        for i in range(n):
            md.set8(0xFFA038, 5); md.set8(0xFFA0B8, 5)
            md.run(1)
    run('RIGHT', 'RIGHT', 150)
    grab('together P1x=%d P2x=%d' % (md.ram16(0xFFA010) - md.ram16(CAMX), md.ram16(0xFFA090) - md.ram16(CAMX)))
    run('RIGHT', 'LEFT', 240)
    grab('P1 right/P2 left: P1x=%d P2x=%d' % (md.ram16(0xFFA010) - md.ram16(CAMX), md.ram16(0xFFA090) - md.ram16(CAMX)))
    run('LEFT', 'RIGHT', 360)
    grab('P1 left/P2 right: P1x=%d P2x=%d' % (md.ram16(0xFFA010) - md.ram16(CAMX), md.ram16(0xFFA090) - md.ram16(CAMX)))
    run('RIGHT', 'RIGHT', 200)
    grab('together')


def s_edges(md, grab):
    """level start / level end of W1 act 0 (mask rule)."""
    grab('start')
    y = md.ram16(CAMY)
    drive_cam(md, 60, y); grab()
    drive_cam(md, md.ram16(CAM_MAXX), y); grab('end')
    drive_cam(md, md.ram16(CAM_MAXX) - 30, y); grab()


def s_room(md, grab):
    """single-screen boss room (camera X range 0..0): fully masked to 320."""
    md.run(120); grab()
    md.hold('RIGHT'); md.run(60); grab()


def s_vertical(md, grab):
    """W2 act 15 (1728 x 1312): vertical + horizontal camera sweep."""
    for tx, ty in ((0, 900), (600, 500), (1200, 100), (1728, 0), (900, 700)):
        drive_cam(md, tx, ty)
        grab('drive')


def s_sky(md, grab):
    """W2 act 0: cliff then clouds (parallax plane B bands)."""
    y = md.ram16(CAMY)
    for tx in (300, 900, 1500, 2100, 2700):
        drive_cam(md, tx, y)
        grab('drive')


def s_water(md, grab):
    """W3 act 0: underwater (per-line hscroll)."""
    y = md.ram16(CAMY)
    for tx in (200, 700, 1200, 1700):
        drive_cam(md, tx, y)
        grab('drive')


SCENARIOS = [
    ('forest', 1, 0, 1, s_forest),
    ('backtrack', 1, 3, 1, s_left),
    ('2P', 1, 0, 3, s_2p),
    ('edges', 1, 0, 1, s_edges),
    ('boss-room', 1, 5, 1, s_room),
    ('vertical', 2, 15, 1, s_vertical),
    ('sky', 2, 0, 1, s_sky),
    ('water', 3, 0, 1, s_water),
]

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for name, w, e, mode, fn in SCENARIOS:
        b, lb = scenario(name, w, e, mode, fn, False)
        a, la = scenario(name, w, e, mode, fn, True)
        n = max(len(a), len(b))
        path = os.path.join(OUT, 'woi_wide_demo_%s.png' % name)
        grid(b + a, n, labels=lb + la, path=path, marks=[E, 320 + E])
        print('wrote', path)
