"""Trailer gameplay without cheats: a search finds inputs that play a clip cleanly (no hits, no lost
lives, no falls) and keep the player moving, then the plan is replayed from the start state without
any loads (as the app will replay it), checked, and written as <clip>.state + <clip>.pads in
trailer_work/cap for the app's autotest "loadstate" / "pads" commands.

  python bot.py <clip> [<clip> ...]        (clips are defined in clips.py)

Same method as the Buster Busts Loose trailer bot: CH-frame chunks; every candidate move is tried from
the chunk's start and scored by how far it gets plus the best of a few short continuations; unsafe moves
score nothing, moves other than plain running pay a little; with no safe move the search backs up.
A clip can switch widescreen mode at a chunk boundary ('switch': (frame, ext)); the app does the same
with "set widescreen=" at that frame.
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import *  # noqa: E402,F403
from clips import CLIPS  # noqa: E402

CH = 20
PENALTY = 6          # px a move other than running must gain to be chosen


def s16(v): return v - 0x10000 if v & 0x8000 else v


# ---- per game: player position, health, lives, "something went wrong" flags, mode / room ----
def st_coi(md):
    return dict(x=s16(md.ram16(0xFFDC10)), y=s16(md.ram16(0xFFDC14)), hp=[md.ram16(0xFFF322)], lives=md.ram16(0xFFF324),
                bad=md.ram8(0xFFDC31) & 0x06, mode=md.ram16(0xFFF300), room=md.ram16(0xFFFF42))


def st_qs(md):
    return dict(x=s16(md.ram16(0xFFA2D0)), y=s16(md.ram16(0xFFA2D4)), hp=[md.ram16(0xFF8F96)], lives=md.ram16(0xFF8F98),
                bad=md.ram8(0xFFEE58), mode=md.ram8(0xFFFF19), room=(md.ram16(0xFF8F34), md.ram16(0xFF8F36)))


def st_woi(md):
    objs = [0xFFA000, 0xFFA080] if md.two else [0xFFA000]
    xs = [s16(md.ram16(o + 0x10)) for o in objs]
    return dict(x=min(xs), xs=xs, y=s16(md.ram16(0xFFA014)), hp=[md.ram8(o + 0x38) for o in objs],
                lives=md.ram16(0xFFFD52),
                bad=sum((md.ram8(o + 0x6D) & 0x03) | (md.ram8(o + 0x3F) & 0x02) | (md.ram8(o + 0x0F) & 0x01) for o in objs),
                mode=md.ram16(0xFFFFD0), room=(md.ram16(0xFFFFDA), md.ram16(0xFFFFDE)))


STATUS = {'coi': st_coi, 'qs': st_qs, 'woi': st_woi}


def status(md):
    return STATUS[md.game](md)


def ok(a, b):
    return (all(y >= x for x, y in zip(a['hp'], b['hp'])) and b['lives'] >= a['lives'] and not b['bad']
            and b['mode'] == a['mode'] and b['room'] == a['room'])


# ---- moves ----
CANDS = [
    ('run',    [('RIGHT', 20)]),
    ('jump',   [('RIGHT+C', 10), ('RIGHT', 10)]),
    ('hjump',  [('RIGHT+C', 20)]),
    ('attack', [('RIGHT+B', 4), ('RIGHT', 16)]),
    ('jatk',   [('RIGHT+C', 8), ('RIGHT+B', 4), ('RIGHT', 8)]),
    ('rjump',  [('RIGHT+C', 6), ('RIGHT', 4), ('RIGHT+C', 10)]),
    ('bjump',  [('LEFT', 8), ('RIGHT+C', 12)]),
    ('wait',   [('', 20)]),
    ('vjump',  [('C', 20)]),
    ('back',   [('LEFT', 20)]),
]
FLY = [('fly', [('RIGHT', 20)]), ('up', [('RIGHT+UP', 20)]), ('down', [('RIGHT+DOWN', 20)]), ('hover', [('', 20)]),
       ('rise', [('UP', 20)]), ('sink', [('DOWN', 20)])]
LOOKS = [[('RIGHT', 40)], [('RIGHT+C', 14), ('RIGHT', 26)], [('RIGHT+C', 30), ('RIGHT', 10)], [('', 40)],
         [('RIGHT+B', 4), ('RIGHT', 36)]]
FLY_LOOKS = [[('RIGHT', 40)], [('RIGHT+UP', 40)], [('RIGHT+DOWN', 40)]]


def mirror(seq, c):
    if c.get('dir', 'right') == 'right': return seq
    sw = lambda b: '+'.join({'RIGHT': 'LEFT', 'LEFT': 'RIGHT'}.get(k, k) for k in b.split('+')) if b else b
    return [(sw(b), n) for b, n in seq]


def progress(s, c):
    return s['x'] if c.get('dir', 'right') == 'right' else -s['x']


def ext_at(c, f):
    sw = c.get('switch')
    return sw[1] if sw and f >= sw[0] else c['ext']


def run_seq(md, seq):
    for btn, n in seq:
        p = pad(btn)
        md.hold(p); md.hold2(p if md.two else 0)
        md.mrun(n)


def load(md, st, ext=None):
    if ext is not None: md.ext = ext
    md.load(st); reconf(md)


def switch_to(md, ext):
    if md.ext != ext:
        md.ext = ext; reconf(md)


def search_from(md, c, name, state, frame0, total):
    cands = [(k, mirror(seq, c)) for k, seq in (FLY if c.get('fly') else CANDS)]
    looks = [mirror(l, c) for l in (FLY_LOOKS if c.get('fly') else LOOKS)]
    path, tried, states, scores = [], [None], [state], []
    backs = 0
    while frame0 + len(path) * CH < total:
        d = len(path)
        e = ext_at(c, frame0 + d * CH)
        if tried[d] is None:
            res = []
            for k, (cname, seq) in enumerate(cands):
                load(md, states[d], e)
                before = status(md)
                run_seq(md, seq)
                after = status(md)
                if not ok(before, after): continue
                mid = md.save()
                best = None
                for look in looks:
                    load(md, mid, e)
                    run_seq(md, look)
                    s = status(md)
                    if ok(after, s) and (best is None or progress(s, c) > best): best = progress(s, c)
                if best is None: continue
                res.append((max(best, progress(after, c)) - (PENALTY if k else 0), -k, k, mid, progress(after, c)))
            res.sort(key=lambda r: (r[0], r[1]), reverse=True)
            tried[d] = res
        if not tried[d]:
            if d == 0: print(name, 'no clean path at frame', frame0, flush=True); return None
            path.pop(); tried.pop(); states.pop(); scores.pop()
            tried[-1] = tried[-1][1:]
            backs += 1
            if backs > 600: print(name, 'too many backtracks', flush=True); return None
            continue
        score, _, k, mid, x = tried[d][0]
        path.append(k); states.append(mid); scores.append(x); tried.append(None)
    print(name, 'searched frames %d-%d: %d chunks, %d backtracks, progress %s -> %s' % (
        frame0, total, len(path), backs, scores[0] if scores else '-', scores[-1] if scores else '-'), flush=True)
    seq = []
    for k in path: seq += cands[k][1]
    return seq


def prefix(seq, frames):
    """the plan's first `frames` frames (the last step cut short if needed)"""
    out, f = [], 0
    for btn, n in seq:
        if f >= frames: break
        n = min(n, frames - f)
        out.append((btn, n)); f += n
    return out


def replay(md, c, start, seq, check=True):
    """the plan from the start state without loads (switching widescreen where the clip does): the first chunk
    that goes wrong (or None) and the states at every chunk"""
    load(md, start, c['ext'])
    f, k, bad, saved = 0, 0, None, {0: md.save()}
    last = status(md)
    while k < len(seq):
        btn, n = seq[k]
        run_seq(md, [(btn, n)]); f += n; k += 1
        if f % CH == 0:
            s = status(md)
            if check and not ok(last, s): bad = f // CH - 1; break
            last = s
            saved[f // CH] = md.save()
            switch_to(md, ext_at(c, f))
    return bad, saved


def new_md(c):
    md = session(c['game'], c['ext'], c.get('cheats', ()))
    md.two = c.get('var', 0) == 2 and c['game'] == 'woi'
    return md


def total_frames(c):
    """emulated frames the plan covers: pre-roll + recording (4 per recorded frame in fast forward)"""
    if c.get('rewind'): return c.get('pre', 0) + c['rewind'][0]
    return c.get('pre', 0) + c['n'] * (4 if c.get('ff') else 1)


def plan(name, c):
    md = new_md(c)
    if not md.start_level(c['level'], c.get('var', 0)):
        print(name, 'START FAILED', flush=True); return None
    run_seq(md, c.get('intro', [('', 30)]))
    start = md.save()
    total = total_frames(c)
    seq = search_from(md, c, name, start, 0, total)
    for attempt in range(10):
        if seq is None: return None
        bad, saved = replay(md, c, start, seq)
        if bad is None: break
        back = max(0, bad - 2)
        print(name, 'replay went wrong in chunk %d; searching again from chunk %d' % (bad, back), flush=True)
        tail = search_from(md, c, name, saved[back], back * CH, total)
        if tail is None: return None
        seq = prefix(seq, back * CH) + tail
    else:
        print(name, 'still going wrong after 10 tries', flush=True); return None
    seq = prefix(seq, total)
    pads = []
    for btn, n in seq:
        p = pad(btn)
        pads += [(p, p if md.two else 0)] * n
    pads = pads[:total]
    load(md, start, c['ext'])
    s0 = status(md)
    write_state(md, name)
    write_pads(pads, name)
    replay(md, c, start, seq, check=False)
    s1 = status(md)
    h = fnv(md)
    json.dump(dict(clip=name, frames=len(pads), pre=c.get('pre', 0), hash='%x' % h, seq=seq),
              open(os.path.join(CAP, name + '.json'), 'w'))
    print(name, 'plan ok: %d frames, hp %s -> %s, lives %d -> %d, x %d -> %d, ram %x' % (
        len(pads), s0['hp'], s1['hp'], s0['lives'], s1['lives'], s0['x'], s1['x'], h), flush=True)
    return seq


if __name__ == '__main__':
    for name in sys.argv[1:]:
        plan(name, CLIPS[name])
