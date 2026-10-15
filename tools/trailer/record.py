"""Records trailer footage with the real app (hidden autotest window, 1920x1080):
  python record.py <clip> [...]     gameplay clips from clips.py, replayed from the bot's state + pads
                                    (plus each clip's extra takes with other settings: <clip>_<take>)
Each recording runs in its own copy of the exe (trailer_work/inst/<name>) with its own settings.ini, launched
from the project folder so the ROMs are found. Output in trailer_work/cap: <name>.mkv (lossless), .pcm, .txt.
After the replay the app logs a RAM fingerprint (marks.txt); it must equal the bot's.
"""
import os, sys, json, shutil, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import ROOT, WORK, CAP, WIDE_SETTING  # noqa: E402
from clips import CLIPS  # noqa: E402

EXE = os.path.join(ROOT, 'build', 'bin', 'DisneyClassics.exe')
LAUNCH_WAIT = 90          # loops from pressing Enter on the launcher card to loading the state


def settings_ini(game, video=None, general=None, extra=None):
    v = {'fullscreen': 0, 'window_scale': 3, 'widescreen': 2, 'hud_anchor': 1, 'filter': 1, 'color': 0}
    v.update(video or {})
    g = {'last_game': game, 'fast_forward': 1, 'rewind': 1}
    g.update(general or {})
    lines = ['[video]'] + ['%s=%s' % kv for kv in v.items()] + ['[general]'] + ['%s=%s' % kv for kv in g.items()]
    for sec, kv in (extra or {}).items():
        lines.append('[%s]' % sec); lines += ['%s=%s' % x for x in kv.items()]
    return '\n'.join(lines) + '\n'


def gameplay_script(name, out, c, frames):
    L = 40 + LAUNCH_WAIT
    pre = c.get('pre', 0)
    s = ['40 tap enter', '%d loadstate %s' % (L, name), '%d pads %s' % (L, name)]
    if c.get('switch'):
        s.append('%d set widescreen=%d' % (L + c['switch'][0], WIDE_SETTING[c['switch'][1]]))
    s.append('%d record %s' % (L + pre, out))
    if c.get('ff'):
        s += ['%d down tab' % (L + pre), '%d up tab' % (L + pre + c['n'])]
        end = L + pre + c['n']
    elif c.get('rewind'):
        at, loops = c['rewind']
        s += ['%d down r' % (L + pre + at), '%d up r' % (L + pre + at + loops)]
        end = L + pre + at
    else:
        end = L + frames
    s += ['%d mark %s_end' % (end, out),
          '%d stoprec' % (L + pre + c['n']),
          '%d quit' % (max(end, L + pre + c['n']) + 5)]
    return '\n'.join(s) + '\n'


def launch(name, ini_text, script_text):
    inst = os.path.join(WORK, 'inst', name)
    os.makedirs(inst, exist_ok=True)
    shutil.copy2(EXE, os.path.join(inst, 'DisneyClassics.exe'))
    with open(os.path.join(inst, 'settings.ini'), 'w') as f: f.write(ini_text)
    sp = os.path.join(WORK, 'scripts', name + '.txt')
    with open(sp, 'w') as f: f.write(script_text)
    return subprocess.Popen([os.path.join(inst, 'DisneyClassics.exe'), '--autotest', CAP, sp,
                             '--capture-size', '1920x1080'], cwd=ROOT)


def marks():
    p = os.path.join(CAP, 'marks.txt')
    out = {}
    if os.path.exists(p):
        for line in open(p):
            w = line.split()
            if 'ram' in w: out[w[0]] = w[w.index('ram') + 1]
    return out


def jobs(name):
    c = CLIPS[name]
    video = {'widescreen': WIDE_SETTING[c['ext']]}
    out = [(name, video)]
    for take, kv in c.get('takes', {}).items():
        v = dict(video); v.update(kv)
        out.append((name + '_' + take, v))
    return out


def record(names, parallel=6):
    running = []
    def reap(block):
        while running and (block or running[0][2].poll() is not None):
            out, info, p = running.pop(0); p.wait(); check(out, info)
            if not block: break
    for name in names:
        c = CLIPS[name]
        info = json.load(open(os.path.join(CAP, name + '.json')))
        for out, video in jobs(name):
            extra = {'cheats.' + c['game']: {k: 1 for k in c['cheats']}} if c.get('cheats') else None
            p = launch(out, settings_ini(c['game'], video, extra=extra), gameplay_script(name, out, c, info['frames']))
            running.append((out, info, p))
            while len(running) >= parallel:
                out0, info0, p0 = running.pop(0); p0.wait(); check(out0, info0)
    while running:
        out0, info0, p0 = running.pop(0); p0.wait(); check(out0, info0)


def check(out, info):
    m = marks().get(out + '_end')
    tp = os.path.join(CAP, out + '.txt')
    t = open(tp).read().split() if os.path.exists(tp) else ['?'] * 3
    verdict = 'OK' if m == info['hash'] else 'MISMATCH'     # fast forward: after pre + 4n frames; rewind: before R
    print('%-18s frames %s  ram app %s bot %s  %s' % (out, t[2], m, info['hash'], verdict), flush=True)


if __name__ == '__main__':
    record(sys.argv[1:])
