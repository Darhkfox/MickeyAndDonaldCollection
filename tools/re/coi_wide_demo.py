"""Castle of Illusion widescreen review: applies the patches from docs/re_coi_wide.md, renders E=40 and
writes before/after strips.

    python tools/re/coi_wide_demo.py [E] [outdir]        (default E=40, outdir tools/re/coi_wide_out)

"before" = original ROM rendered with E extra pixels per side (raw).
"after"  = patched ROM (patches(E) + camera clamp patch C) rendered the same way and post-processed with
           the runtime rules (edge mask, HUD remap, pillarbox outside gameplay) = what the port should show.
Magenta dots mark the old 320-px borders.  Also prints plane-validator statistics.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coi_wide_common as W  # noqa: E402
from mdt import MD, COI  # noqa: E402

E = int(sys.argv[1]) if len(sys.argv) > 1 else 40
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(W.HERE, 'coi_wide_out')
os.makedirs(OUT, exist_ok=True)


def make(after):
    md = W.new()
    if after:
        W.apply_patches(md, E, clamp=True)
    md.set_ext(E, sprite_limit=False)
    return md


def img(md, after):
    im = W.compose(md, E) if after else W.shot(md)
    return W.mark_edges(im, E)


def pair_run(name, setup, steps, cols=2, note=''):
    """setup(md) prepares a machine, steps is a list of callables step(md) run on both machines;
    a frame is captured after each step.  Writes OUT/name.png: rows = steps, columns = before | after."""
    mds = []
    for after in (False, True):
        md = make(after)
        setup(md)
        mds.append(md)
    ims = []; labels = []
    for i, st in enumerate(steps):
        for k, md in enumerate(mds):
            st(md)
            ims.append(img(md, k == 1))
            c = W.cam(md)
            labels.append('%s %s  cam %d,%d  $F300=%02X' % ('AFTER ' if k else 'BEFORE', name, c['x'], c['y'], md.ram16(W.MODE)))
    W.strip(ims, os.path.join(OUT, name + '.png'), cols=cols, labels=labels)
    print('wrote', name + '.png', note)
    return mds


def cam_steps(xs, y=None, speed=3):
    """Camera-driven path (list of target X) split into capture steps; y=None keeps the camera Y."""
    out = []
    for x in xs:
        def st(md, x=x):
            W.camera_driver(md, True)
            yy = W.cam(md)['y'] if y is None else y
            W.cam_path(md, [(x, yy)], speed=speed)
            W.hold_cam(md, 1)
        out.append(st)
    return out


def seg(a, b, n):
    return [a + (b - a) * i // n for i in range(1, n + 1)]


def main():
    t0 = time.time()
    # 1. real play, Enchanted Forest 1: run/jump right, then back left (player input, no noclip)
    def play_right(md):
        for _ in range(2):
            md.hold('RIGHT+C'); md.run(25); md.hold('RIGHT'); md.run(25)
        W.invuln(md)

    def play_left(md):
        for _ in range(2):
            md.hold('LEFT+C'); md.run(25); md.hold('LEFT'); md.run(25)
        W.invuln(md)
    pair_run('coi_wide_1_forest_play', lambda md: W.goto_level(md, 0),
             [lambda md: md.run(1)] + [play_right] * 8 + [play_left] * 6,
             note='(real input: level start clamp/mask, HUD remap, objects both directions)')

    # 2. objects when scrolling left (Dessert 1): pop-in at the left edge before, none after
    def setup15(md):
        W.goto_level(md, 0x15); W.camera_driver(md, True)
        W.cam_path(md, [(2400, 232)], speed=8)
    pair_run('coi_wide_2_dessert_left', setup15, cam_steps(seg(2400, 1200, 16), 232),
             note='(camera driven 3 px/frame to the left)')

    # 3. Castle 1 scrolling right (enemies entering from the right extension)
    def setup17(md):
        W.goto_level(md, 0x17); W.camera_driver(md, True)
    pair_run('coi_wide_3_castle_right', setup17, cam_steps(seg(40, 1000, 12)),
             note='(camera driven to the right)')

    # 4. Toyland 2 animated map blocks at the right/left extension
    def setup6(md):
        W.goto_level(md, 6); W.camera_driver(md, True)
        W.cam_path(md, [(652, 232)], speed=6)
    pair_run('coi_wide_4_toyland_anim', setup6, [lambda md: W.hold_cam(md, 12)] * 6,
             note='(blue blocks at the left edge animate only after)')

    # 5. level starts / locked arenas
    ims = []; labels = []
    for n in (0x00, 0x06, 0x0C, 0x19, 0x04, 0x08, 0x16, 0x1B):
        for after in (False, True):
            md = make(after)
            W.goto_level(md, n, 200)
            ims.append(img(md, after))
            r = W.port_rules(md, E, clamp=after)
            labels.append('%s round %02X %s  mask %d/%d' % ('AFTER ' if after else 'BEFORE', n, W.ROUND_NAMES[n], r['maskL'], r['maskR']))
    W.strip(ims, os.path.join(OUT, 'coi_wide_5_edges.png'), cols=2, labels=labels)
    print('wrote coi_wide_5_edges.png')

    # 6. HUD remap at E=40 and E=20
    ims = []
    for e in (40, 20):
        md = W.new(); md.set_ext(e, sprite_limit=False); W.goto_level(md, 0x15)
        raw = W.shot(md)
        ims.append(raw.crop((0, 176, raw.width, 224)).resize((raw.width * 2, 96)))
        ims.append(W.hud_remap(raw, e).crop((0, 176, raw.width, 224)).resize((raw.width * 2, 96)))
    W.strip(ims, os.path.join(OUT, 'coi_wide_6_hud.png'), cols=1,
            labels=['E=40 before', 'E=40 remapped', 'E=20 before', 'E=20 remapped'])
    print('wrote coi_wide_6_hud.png')

    # 7. rotating room (Toyland 3) flip effect, after only
    md = make(True)
    W.goto_level(md, 7); W.camera_driver(md, True); W.cam_path(md, [(1000, 0)], speed=6); W.camera_driver(md, False)
    md.run(4); md.set8(0xFFF442, 1)
    ims = []; labels = []
    for i in range(8):
        md.run(8); ims.append(img(md, True)); labels.append('flip +%d $F300=%02X $9801=%d' % (i * 8, md.ram16(W.MODE), md.ram8(0xFF9801)))
    md.hold('LEFT'); md.run(90); md.hold(0)
    ims.append(img(md, True)); labels.append('upside-down, walked left')
    W.strip(ims, os.path.join(OUT, 'coi_wide_7_flip.png'), cols=3, labels=labels)
    print('wrote coi_wide_7_flip.png')

    # 8. non-gameplay screens (before raw / after pillarboxed): title, intro, options, cutscene, hub, gameplay, game over
    def wait_state(md, states, limit=3000, press=None):
        for _ in range(limit // 10):
            if md.ram16(W.MODE) in states:
                return True
            if press:
                md.press(press, 4, 6)
            else:
                md.run(10)
        return False
    ims = []; labels = []
    names = ['intro text ($F300=06)', 'title logo (end of intro, $06)', 'options ($0A)', 'pre-game cutscene ($2A)',
             'castle hall hub ($22)', 'gameplay ($0E)', 'game over ($1A)']
    for after in (False, True):
        md = MD(COI)
        if after:
            W.apply_patches(md, E, clamp=True)
        md.set_ext(E, sprite_limit=False)
        md.reset()
        wait_state(md, (6,)); md.run(200); ims.append(img(md, after))
        md.run(3000); ims.append(img(md, after))
        wait_state(md, (0x0A,), press='START'); md.run(60); ims.append(img(md, after))
        wait_state(md, (0x2A,), press='START'); md.run(300); ims.append(img(md, after))
        wait_state(md, (0x22,), limit=6000); md.run(300); ims.append(img(md, after))
        md.hold('UP'); md.run(30); md.hold(0)
        wait_state(md, (0x0E,), limit=3000); md.run(120); ims.append(img(md, after))
        md.set8(0xFFF449, 1); md.set8(0xFFF40F, 1); wait_state(md, (0x1A,)); md.run(400); ims.append(img(md, after))
        labels += [('AFTER ' if after else 'BEFORE ') + n for n in names]
    k = len(names)
    order = [i for j in range(k) for i in (j, j + k)]
    W.strip([ims[i] for i in order], os.path.join(OUT, 'coi_wide_8_screens.png'), cols=2, labels=[labels[i] for i in order])
    print('wrote coi_wide_8_screens.png')

    # 9. validator statistics (after): serpentine camera sweep over selected rounds
    print('plane validator, patched + clamp, E=%d (frames with wrong cells in the extensions / centre):' % E)
    for n in (0x00, 0x05, 0x06, 0x09, 0x0D, 0x17, 0x19):
        md = make(True); W.goto_level(md, n); W.camera_driver(md, True)
        c = W.cam(md)
        ys = list(range(c['miny'], c['maxy'] + 1, 160)) or [c['miny']]
        pts = []
        for i, y in enumerate(ys):
            pts += [(c['maxx'], y), (c['minx'], y)] if i % 2 == 0 else [(c['minx'], y), (c['maxx'], y)]
        bad = [0, 0, 0]
        def cb(md, step):
            r = W.check_planes(md, E)
            bad[0] += 1
            bad[1] += any(v['left'] + v['right'] for v in r.values())
            bad[2] += any(v['center'] for v in r.values())
        W.cam_path(md, pts, speed=6, every=3, cb=cb)
        print('  round %02X %-28s frames %5d  ext-bad %d  centre-bad %d' % (n, W.ROUND_NAMES[n], bad[0], bad[1], bad[2]))
    print('done in %.0f s, output in %s' % (time.time() - t0, OUT))


if __name__ == '__main__':
    main()
