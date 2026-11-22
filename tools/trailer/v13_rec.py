"""Footage for the v1.3 update trailer (trailer_v13.py), recorded with the real app like v11_rec.py (hidden 1920x1080
autotest, one exe copy per recording, plus the Theatre style's title font; output in trailer_work/cap):
  python v13_rec.py <name> [...]          names: see REC below ('all' = every one)
The "before" takes run the v1.2 release's exe (dist/...v1.2-win64). Gameplay clips come from bot.py (clips.py, v13_*);
v13_storm is planned at 4:3 and replayed from each mode's own level-start state (made headless: v13_states()).
"""
import os, sys, json, shutil, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
os.environ['MDTOOL_DLL'] = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'build', 'tools', 'mdtool.dll')   # (v1.3's)
sys.path.insert(0, HERE)
from tb import ROOT, WORK, CAP, WIDE_SETTING  # noqa: E402
from clips import CLIPS  # noqa: E402
from record import settings_ini, gameplay_script, EXE  # noqa: E402
from ui import S, in_game  # noqa: E402

EXE_V12 = os.path.join(ROOT, r'dist\Mickey-and-Donald-Collection-v1.2-win64\Mickey and Donald Collection\DisneyClassics.exe')
FONT = os.path.join(ROOT, r'third_party\fonts\LuckiestGuy-Regular.ttf')
COVERS = os.path.join(WORK, r'v13\covers')
# v1.3 pause menu, achievements off: the rows the cursor stops on
PAUSE = ['Resume', 'Slot', 'Save State', 'Load State', 'Level Select', 'Cheats', 'Achievements', 'Sound Test',
         'Music Player', 'Options', 'Credits', 'Reset Game', 'Return to Launcher', 'Quit to Desktop']


def launch(name, ini_text, script_text, exe=EXE, covers=False, files=None):
    inst = os.path.join(WORK, 'inst', name)
    if os.path.exists(inst): shutil.rmtree(inst)
    os.makedirs(inst)
    shutil.copy2(exe, os.path.join(inst, 'DisneyClassics.exe'))
    shutil.copy2(FONT, inst)
    if covers: shutil.copytree(COVERS, os.path.join(inst, 'covers'))
    for fname, text in (files or {}).items():
        open(os.path.join(inst, fname), 'w').write(text)
    open(os.path.join(inst, 'settings.ini'), 'w').write(ini_text)
    sp = os.path.join(WORK, 'scripts', name + '.txt')
    open(sp, 'w').write(script_text)
    return subprocess.Popen([os.path.join(inst, 'DisneyClassics.exe'), '--autotest', CAP, sp,
                             '--capture-size', '1920x1080'], cwd=ROOT)


def ini(game, wide=2, general=None, video=None, extra=None, launcher=None):
    v = {'widescreen': wide}
    v.update(video or {})
    g = {'achievements': 0}
    g.update(general or {})
    ex = dict(extra or {})
    ex['launcher'] = dict({'menu_style': 0, 'covers': 0, 'background': 0, 'cover_fill': 0}, **(launcher or {}))
    ex['updates'] = {'check': 0}
    return settings_ini(game, v, g, ex)


def to_options(s):
    """launcher: down to the buttons, left twice: Options"""
    return s.tap('down', gap=30).tap('left', gap=30).tap('left', gap=30).tap('enter', gap=60)


# ---------------------------------------------------------------- launcher and menus
def launcher_theatre():
    s = S(30).wait(2900).cmd('record v13_launcher')
    for k in ['right', 'right', 'left', 'left', 'right']:
        s.wait(150).tap(k, gap=0)
    s.wait(200).cmd('stoprec')
    return launch('v13_launcher', ini('coi'), s.text())


def style_switch():
    """Options > Launcher & Menus > Menu Style: Theatre -> Classic (back on the launcher), then Theatre again"""
    s = S(30).wait(2400).cmd('record v13_style').wait(40)
    to_options(s).tap('down', 3, gap=24).tap('enter', gap=70)
    s.tap('right', gap=110).tap('esc', gap=50).tap('esc', gap=170)          # Classic, back to the launcher
    to_options(s).tap('down', 3, gap=24).tap('enter', gap=60)
    s.tap('right', gap=90).tap('esc', gap=40).tap('esc', gap=150).cmd('stoprec')
    return launch('v13_style', ini('coi'), s.text())


def covers():
    """covers + background (whole covers), then Cover Shape: Fill the Frame"""
    s = S(30).wait(300).cmd('record v13_covers').wait(60)
    for k in ['right', 'right', 'left']:
        s.wait(90).tap(k, gap=0)
    s.wait(90)
    to_options(s).tap('down', 3, gap=24).tap('enter', gap=60)
    s.wait(40).tap('down', 3, gap=30).wait(30).tap('right', gap=60).tap('esc', gap=40).tap('esc', gap=150)
    s.tap('up', gap=60).tap('right', gap=80).tap('right', gap=120).cmd('stoprec')
    return launch('v13_covers', ini('coi', launcher={'covers': 1, 'background': 1}), s.text(), covers=True)


def pause_menu():
    """QuackShot paused: the Theatre pause menu, Options > Video, the new QuackShot option switched on"""
    s = in_game(S(), 'qs_ultra').cmd('record v13_menus').wait(30)
    s.tap('esc', gap=60).tap('down', PAUSE.index('Options'), gap=12).wait(30).tap('enter', gap=60)
    s.tap('enter', gap=60).tap('down', 5, gap=20).wait(70).tap('right', gap=120)
    s.tap('esc', gap=30).tap('esc', gap=30).tap('esc', gap=40).cmd('stoprec')
    return launch('v13_menus', ini('qs', 4), s.text())


SEED = '\n'.join(['[unlocked]', 'coi_timber=2026-10-20', 'coi_toyland=2026-10-21', 'coi_double=2026-10-21',
                  'coi_apple=2026-10-22', 'qs_herokey=2026-10-21', 'qs_gyro=2026-10-23', 'qs_quack=2026-10-24',
                  'woi_deck=2026-11-01', 'woi_forest=2026-11-02', '', '[progress]', 'coi_juggler=12', 'qs_flyer=4', '',
                  '[general]', 'notice_shown=1', ''])


def export():
    """launcher > Achievements, scroll, Export to CSV"""
    s = S(30).wait(200).cmd('record v13_export').wait(30)
    s.tap('down', gap=30).tap('left', gap=30).tap('enter', gap=90)
    s.tap('down', 6, gap=14).wait(40).tap('up', 6, gap=10).wait(30).tap('down', gap=40).tap('enter', gap=200)
    s.tap('esc', gap=40).cmd('stoprec')
    return launch('v13_export', ini('coi'), s.text(), files={'achievements.ini': SEED})


# ---------------------------------------------------------------- QuackShot
def qs_intro():
    """QuackShot from power-on at 21:9: the attract intro and title, START, the menu, a new game (one START skips the
    story) to the map, and the airplane's flight (times from a headless run of the same presses)"""
    L = 41                                      # the game powers on the loop after the launcher's Enter
    s = S(40).tap('enter', gap=0)
    s.f = L + 280; s.cmd('record v13_qs_intro')
    for t in (2900, 3026, 3152, 3242):
        s.f = L + t; s.down('enter'); s.f += 6; s.up('enter')
    s.f = L + 3480; s.cmd('stoprec')
    return launch('v13_qs_intro', ini('qs', 4), s.text())


def gameplay(name, out=None, exe=EXE, wide=None, video=None, cheats=None, state=None, extra=None):
    c = CLIPS[name]
    info = json.load(open(os.path.join(CAP, name + '.json')))
    out = out or name
    text = gameplay_script(state or name, out, c, info['frames'])
    if state: text = text.replace('pads %s' % state, 'pads %s' % name)
    ex = dict(extra or {})
    ch = list(c.get('cheats', ())) + list(cheats or ())
    if ch: ex['cheats.' + c['game']] = {k: 1 for k in ch}
    return launch(out, ini(c['game'], WIDE_SETTING[c['ext']] if wide is None else wide, video=video, extra=ex), text, exe)


# ---------------------------------------------------------------- Castle of Illusion #19: per-mode level-start states
def v13_states():
    """v13_storm's start state in each mode: 4:3 (the bot's own), 16:9 with the fix, 16:9 as v1.2 (module hooks off)"""
    import tb
    from tb import pad, write_state
    c = CLIPS['v13_storm']
    for out, ext, hooks in (('v13_storm_w', 40, True), ('v13_storm_o', 40, False)):
        md = tb.session('coi', ext)
        if not hooks: md.module_hooks_off()
        md.start_level(c['level'], c.get('var', 0))
        for btn, n in c.get('intro', [('', 30)]): md.hold(pad(btn)); md.mrun(n)
        write_state(md, out)
    print('states written')


REC = {
    'v13_launcher':  launcher_theatre,
    'v13_style':     style_switch,
    'v13_covers':    covers,
    'v13_menus':     pause_menu,
    'v13_export':    export,
    'v13_qs_intro':  qs_intro,
    # #19 before / after: the same 4:3-planned inputs at 4:3, 16:9 (v1.3) and 16:9 (v1.2)
    'v13_storm_43':  lambda: gameplay('v13_storm', 'v13_storm_43', wide=0),
    'v13_storm_w':   lambda: gameplay('v13_storm', 'v13_storm_w', wide=2, state='v13_storm_w'),
    'v13_storm_o':   lambda: gameplay('v13_storm', 'v13_storm_o', EXE_V12, wide=2, state='v13_storm_o'),
    # CPU Overclock with Show FPS
    'v13_starry':    lambda: gameplay('v13_starry', video={'show_fps': 1}),
    'v13_starry_oc': lambda: gameplay('v13_starry_oc', video={'show_fps': 1, 'cpu_speed': 200}),
    # QuackShot fixes at 21:9 in v1.3 and v1.2, and the spawn option
    'v13_rooftops':  lambda: gameplay('qs_rooftops', 'v13_rooftops'),
    'v13_rooftops_v12': lambda: gameplay('qs_rooftops', 'v13_rooftops_v12', EXE_V12),
    'v13_qs_ultra_fix': lambda: gameplay('qs_ultra', 'v13_qs_ultra_fix', video={'qs_spawn_fix': 1}),
    'v13_qs_ultra':  lambda: gameplay('qs_ultra', 'v13_qs_ultra'),
}


if __name__ == '__main__':
    if sys.argv[1:] == ['states']:
        v13_states(); sys.exit(0)
    names = list(REC) if sys.argv[1:] == ['all'] else sys.argv[1:]
    procs = []
    for name in names:
        procs.append((name, REC[name]()))
        if len(procs) >= 5:
            n0, p0 = procs.pop(0); p0.wait(); print(n0, 'done', p0.returncode, flush=True)
    for name, p in procs:
        p.wait(); print(name, 'done', p.returncode, flush=True)
