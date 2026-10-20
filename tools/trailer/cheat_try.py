"""Try hand-written input plans for the v1.1 trailer's cheat clip (Walk Faster, Moon Jump, Jump in Midair):
  python cheat_try.py <level> <plan name>
Runs the plan headless with the three cheats on, prints Mickey's x / y / power every 20 frames and saves a contact
sheet (trailer_work/preview_v11/cheat/<plan>.png). Plans: PLANS below, [(buttons, frames)].
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ['MDTOOL_DLL'] = r'K:\projects\castle of illusion\build\tools\mdtool.dll'
from tb import session, pad, WORK  # noqa: E402
from PIL import Image  # noqa: E402

CHEATS = os.environ.get('CHEATS', 'walk_fast,moon_jump').split(',')
PLANS = {
    # walk fast, then a long moon jump up, drifting right; then midair hops
    'a': [('', 30), ('RIGHT', 110), ('C', 70), ('RIGHT+C', 40), ('RIGHT', 30), ('', 10), ('C', 6), ('', 10), ('C', 6),
          ('', 10), ('C', 6), ('', 10), ('C', 6), ('RIGHT', 30), ('', 60), ('RIGHT', 100)],
    'm': [('', 30), ('RIGHT', 60), ('C', 120), ('', 80)],
    'mr': [('', 30), ('RIGHT', 60), ('RIGHT+C', 120), ('RIGHT', 80)],
    'c': [('', 30), ('RIGHT', 100), ('RIGHT+C', 110), ('RIGHT', 70), ('RIGHT', 60), ('RIGHT+C', 80), ('RIGHT', 90)],
    'd': [('', 30), ('RIGHT', 100), ('C', 20), ('RIGHT+C', 100), ('RIGHT', 80), ('RIGHT', 60), ('C', 15), ('RIGHT+C', 60), ('RIGHT', 80)],
    'e': [('', 30), ('RIGHT', 100), ('RIGHT+C', 42), ('RIGHT', 70), ('RIGHT', 40), ('RIGHT+C', 46), ('RIGHT', 80),
          ('RIGHT', 30), ('RIGHT+C', 40), ('RIGHT', 120)],
    'b': [('', 30), ('RIGHT', 80), ('RIGHT+C', 90), ('RIGHT', 40), ('', 40), ('RIGHT', 60), ('C', 6), ('', 12), ('C', 6),
          ('', 12), ('C', 6), ('', 12), ('C', 6), ('', 12), ('C', 6), ('', 60), ('RIGHT', 80)],
}


def s16(v): return v - 0x10000 if v & 0x8000 else v


def run(level, name, ext=40):
    md = session('coi', ext, CHEATS)
    assert md.start_level(level)
    out = os.path.join(WORK, 'preview_v11', 'cheat'); os.makedirs(out, exist_ok=True)
    shots, f = [], 0
    for btn, n in PLANS[name]:
        md.hold(pad(btn))
        for _ in range(n):
            md.mrun(1); f += 1
            ymin = min(globals().get('ymin', 999), s16(md.ram16(0xFFDC14)) - s16(md.ram16(0xFFFA80)))
            globals()['ymin'] = ymin
            if f % 20 == 0:
                x, y = s16(md.ram16(0xFFDC10)), s16(md.ram16(0xFFDC14))
                print('%4d %-8s x %5d y %5d cam %5d,%4d power %d tries %d' % (
                    f, btn, x, y, s16(md.ram16(0xFFFA84)), s16(md.ram16(0xFFFA80)), md.ram16(0xFFF322), md.ram16(0xFFF324)))
            if f % 30 == 0:
                p = os.path.join(out, '%s_%04d.png' % (name, f)); md.png(p, 1); shots.append(p)
    ims = [Image.open(p).convert('RGB').resize((400, 224)) for p in shots]
    c = Image.new('RGB', (5 * 404, ((len(ims) + 4) // 5) * 228), (255, 0, 255))
    for i, im in enumerate(ims): c.paste(im, ((i % 5) * 404, (i // 5) * 228))
    c.save(os.path.join(out, '%s_L%d.png' % (name, level)))
    print('frames', f, 'lowest screen y of Mickey (feet)', globals().get('ymin'), 'power', md.ram16(0xFFF322))


if __name__ == '__main__':
    run(int(sys.argv[1]), sys.argv[2])
