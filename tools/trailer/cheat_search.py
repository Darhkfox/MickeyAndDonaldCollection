"""The v1.1 trailer's cheat clip (Walk Faster + Moon Jump, Enchanted Forest): a small grid search over a hand-shaped
plan - run fast, then three moon jumps (hold the jump while running right, release) - for one where Mickey takes no
hit, stays on screen at the top of each jump (feet at screen y >= 40) and gets furthest. Writes the clip like bot.py
(trailer_work/cap/v11_cheats.state / .pads / .json) for v11_rec.py.
  python cheat_search.py
"""
import os, sys, json, itertools
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ['MDTOOL_DLL'] = r'K:\projects\castle of illusion\build\tools\mdtool.dll'
from tb import session, pad, write_state, write_pads, fnv, CAP  # noqa: E402

CHEATS = ['walk_fast', 'moon_jump']
PRE, N = 60, 480          # as clips.py 'v11_cheats'


def s16(v): return v - 0x10000 if v & 0x8000 else v


def plan(run0, h1, g1, h2, g2, h3):
    seq = [('', 30), ('RIGHT', run0), ('RIGHT+C', h1), ('RIGHT', g1), ('RIGHT+C', h2), ('RIGHT', g2), ('RIGHT+C', h3)]
    used = sum(n for _, n in seq)
    seq.append(('RIGHT', PRE + N + 30 - used))
    return seq


def judge(md, start, seq):
    md.load(start); md.configure(ext=40, hud_anchor=True, cheats=CHEATS); md.set_ext(40, sprite_limit=False)
    ymin, f = 999, 0
    for btn, n in seq:
        md.hold(pad(btn))
        for _ in range(n):
            md.mrun(1); f += 1
            if f > PRE + N + 30: break
            ymin = min(ymin, s16(md.ram16(0xFFDC14)) - s16(md.ram16(0xFFFA80)))
            if md.ram16(0xFFF322) < 3 or md.ram16(0xFFF324) < 3: return None
    if ymin < 40: return None
    return s16(md.ram16(0xFFDC10)), ymin


def main():
    md = session('coi', 40, CHEATS)
    assert md.start_level(0)
    start = md.save()
    best = None
    for run0, h1, g1, h2, g2, h3 in itertools.product((70, 90, 110), (34, 40, 46), (40, 60, 80), (34, 40, 46), (40, 60, 80), (34, 42)):
        seq = plan(run0, h1, g1, h2, g2, h3)
        r = judge(md, start, seq)
        if r and (best is None or r[0] > best[0][0]):
            best = (r, seq); print('ok', r, (run0, h1, g1, h2, g2, h3), flush=True)
    if not best: print('nothing clean'); return
    (x, ymin), seq = best
    # the clip: the app loads the state 30 frames in (as bot.py's intro) and replays the pads
    md.load(start); md.configure(ext=40, hud_anchor=True, cheats=CHEATS); md.set_ext(40, sprite_limit=False)
    md.hold(0); md.mrun(30)
    md.game, md.ext, md.cheats = 'coi', 40, CHEATS
    write_state(md, 'v11_cheats')
    pads, rest = [], seq[1:]
    for btn, n in rest: pads += [pad(btn)] * n
    pads = pads[:PRE + N]
    write_pads(pads, 'v11_cheats')
    for p in pads: md.hold(p); md.mrun(1)
    json.dump(dict(clip='v11_cheats', frames=len(pads), pre=PRE, hash='%x' % fnv(md), seq=rest),
              open(os.path.join(CAP, 'v11_cheats.json'), 'w'))
    print('written: x %d, highest feet at screen y %d, ram %x' % (x, ymin, fnv(md)))


if __name__ == '__main__':
    main()
