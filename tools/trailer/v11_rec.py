"""Footage for the v1.1 update trailer (trailer_v11.py), recorded with the real app like record.py / ui.py
(hidden 1920x1080 autotest, one exe copy per recording; output in trailer_work/cap):
  python v11_rec.py <name> [...]          names: see REC below ('all' = every one)
Gameplay clips searched by bot.py (clips.py, v11_*) are replayed from their state + pads; v11_tree_v10 replays the
Evil Tree clip in the v1.0 exe (black bars) for the before / after. MD+ clips get the soundtrack's position put into
their save state's tag first, so the arranged music plays from the first frame.
"""
import os, sys, json, shutil, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import ROOT, WORK, CAP, WIDE_SETTING  # noqa: E402
from clips import CLIPS  # noqa: E402
from record import settings_ini, gameplay_script, EXE  # noqa: E402
from ui import S, in_game  # noqa: E402

EXE_V10 = r'K:\projects\coi_regress_main\build\bin\DisneyClassics.exe'
QA = os.path.join(ROOT, r'tools\qa')
PACK = {'coi': 'MD+ Castle Of Illusion (Grant Kirkhope 2013 Arranged)',
        'qs': 'MD+ Quackshot - Starring Donald Duck - Patch and Soundpack by GMKalandar'}
# v1.1 pause menu (achievements off: every row selectable)
PAUSE = ['Resume', 'Slot', 'Save State', 'Load State', 'Level Select', 'Cheats', 'Achievements', 'Sound Test',
         'Music Player', 'Options', 'Credits', 'Reset Game', 'Return to Launcher', 'Quit to Desktop']


def launch(name, ini_text, script_text, exe=EXE):
    inst = os.path.join(WORK, 'inst', name)
    if os.path.exists(inst): shutil.rmtree(inst)
    os.makedirs(inst)
    shutil.copy2(exe, os.path.join(inst, 'DisneyClassics.exe'))
    open(os.path.join(inst, 'settings.ini'), 'w').write(ini_text)
    sp = os.path.join(WORK, 'scripts', name + '.txt')
    open(sp, 'w').write(script_text)
    return subprocess.Popen([os.path.join(inst, 'DisneyClassics.exe'), '--autotest', CAP, sp,
                             '--capture-size', '1920x1080'], cwd=ROOT)


def ini(game, wide=2, general=None, audio=None, cheats=None, video=None):
    v = {'widescreen': wide}
    v.update(video or {})
    g = {'achievements': 0}
    g.update(general or {})
    extra = {}
    if audio: extra['audio'] = audio
    if cheats: extra['cheats.' + game] = {k: 1 for k in cheats}
    return settings_ini(game, v, g, extra or None)


def pause_to(s, item):
    s.tap('esc', gap=30)
    s.tap('down', PAUSE.index(item), gap=10)
    return s


# ---------------------------------------------------------------- gameplay (bot clips)
def gameplay(name, exe=EXE, out=None, audio=None):
    c = CLIPS[name]
    info = json.load(open(os.path.join(CAP, name + '.json')))
    out = out or name
    text = gameplay_script(name, out, c, info['frames'])
    return launch(out, ini(c['game'], WIDE_SETTING[c['ext']], audio=audio, cheats=c.get('cheats')), text, exe)


def mdplus_state(name, track, loop=True):
    """the clip's state was made on the patched ROM headless (no soundtrack there): put the track the game asked for at
    the level start, and how far it has played by the state's frame, into its tag"""
    c = CLIPS[name]
    p = os.path.join(CAP, name + '.state')
    d = open(p, 'rb').read()
    tag = d[len(d) - 1 - d[-1]:-1].decode()
    if '|t' in tag: return
    core = d[:len(d) - 1 - d[-1]]
    frames = 30 + 30                      # (bot: the intro's 30 frames; the level select hands over ~30 after the request)
    pos = int(frames * 44100 / 59.92)
    parts = tag.split('|')
    while len(parts) < 3: parts.append('')
    tag = '|'.join(parts[:3]) + '||t%d,%d,%d,255' % (track, pos, 1 if loop else 0)
    open(p, 'wb').write(core + tag.encode() + bytes([len(tag)]))


# ---------------------------------------------------------------- app screens
def welcome():
    s = S(30).cmd('welcome').wait(30).cmd('record v11_welcome').wait(420).tap('enter', gap=60).cmd('stoprec')
    return launch('v11_welcome', ini('coi', general={'achievements': 1}), s.text())


AWARDS = ['coi_timber', 'coi_toyland', 'coi_double', 'coi_hattrick', 'coi_noscratch', 'coi_speedy', 'qs_herokey',
          'qs_gyro', 'qs_climber', 'woi_forest', 'woi_together']


def ach_page():
    s = S(30)
    for a in AWARDS: s.cmd('award ' + a).wait(2)
    s.wait(60 * 5 * len(AWARDS))                            # the pop-ups play out
    s.cmd('record v11_achpage').wait(30)
    s.tap('down', gap=40).tap('left', gap=40).tap('enter', gap=120)   # launcher: Achievements
    s.tap('down', 4, gap=40).wait(30).tap('down', 16, gap=9).wait(70).tap('down', 18, gap=9).wait(70)
    s.tap('down', 17, gap=9).wait(90)
    s.tap('esc', gap=40).cmd('stoprec')
    return launch('v11_achpage', ini('coi', general={'achievements': 1}), s.text())


def counting_copy(src, dst):
    """a copy of a save state whose tag says the run counts for achievements (no toast when it is loaded)"""
    d = open(src, 'rb').read()
    core, tag = d[:len(d) - 1 - d[-1]], d[len(d) - 1 - d[-1]:-1].decode()
    parts = (tag.split('|') + ['', ''])[:3]
    tag = '|'.join(parts) + '|a1.00000000'
    open(dst, 'wb').write(core + tag.encode() + bytes([len(tag)]))


def hallway(name, state, start, frames, wide=4, awards=(), exe=EXE):
    """a castle hallway walk from a state made the game's way (tools/qa/wide_hall), no input"""
    counting_copy(os.path.join(QA, r'wide_hall\out', state + '.state'), os.path.join(CAP, name + '.state'))
    s = in_game(S(), name, settle=0)
    s.wait(start).cmd('record ' + name)
    for at, a in awards: s.wait(at).cmd('award ' + a); s.wait(-at)
    s.wait(frames).cmd('stoprec')
    return launch(name, ini('coi', wide, general={'achievements': 1}), s.text(), exe)


def locked():
    """achievements on: the pause menu's Level Select and Cheats greyed out (the cursor skips them), then Options'
    Fast Forward and Rewind"""
    counting_copy(os.path.join(CAP, 'coi_forest.state'), os.path.join(CAP, 'v11_forest_a1.state'))
    s = in_game(S(), 'v11_forest_a1').cmd('record v11_locked').wait(20)
    s.tap('esc', gap=70).tap('down', 4, gap=45).wait(50)                    # Slot .. Load State, then Achievements
    s.tap('down', 3, gap=20).wait(10).tap('enter', gap=150)                  # Sound Test, Music Player, Options
    s.tap('esc', gap=30).tap('esc', gap=40).cmd('stoprec')
    return launch('v11_locked', ini('coi', general={'achievements': 1}), s.text())


def cheats_ui():
    s = in_game(S(), 'coi_forest').cmd('record v11_cheats_ui').wait(30)
    pause_to(s, 'Cheats').wait(20).tap('enter', gap=50)
    s.tap('down', 6, gap=12).wait(20).tap('enter', gap=40).tap('down', gap=25).tap('enter', gap=40)
    s.tap('down', gap=25).tap('enter', gap=60).wait(60).tap('esc', gap=30).tap('esc', gap=60).cmd('stoprec')
    return launch('v11_cheats_ui', ini('coi'), s.text())


def hotkeys():
    s = S(40).cmd('record v11_hotkeys').wait(40)
    s.tap('down', gap=30).tap('left', gap=30).tap('left', gap=30).tap('enter', gap=50)     # launcher: Options
    s.tap('down', 3, gap=20).tap('enter', gap=60)                                           # Controls - Player 1
    s.tap('down', 11, gap=16).wait(40)                                                      # down to Quick Save
    s.tap('enter', gap=40).tap('q', gap=70)                                                 # rebind it to Q
    s.tap('down', 2, gap=30).wait(30).tap('down', 9, gap=14).wait(30)                       # gamepad rows
    s.tap('down', 6, gap=16).wait(90).tap('esc', gap=30).tap('esc', gap=40).cmd('stoprec')
    return launch('v11_hotkeys', ini('coi'), s.text())


def audio_page():
    s = S(40).cmd('record v11_audio').wait(40)
    s.tap('down', gap=30).tap('left', gap=30).tap('left', gap=30).tap('enter', gap=50)     # Options
    s.tap('down', 2, gap=20).tap('enter', gap=70)                                           # Audio
    s.tap('down', 3, gap=24).wait(40).tap('right', gap=70)                                  # Castle of Illusion: MD+
    s.tap('down', gap=30).wait(20).tap('right', gap=80).tap('down', gap=40).wait(60)       # QuackShot: MD+, volume
    s.tap('esc', gap=30).tap('esc', gap=40).cmd('stoprec')
    return launch('v11_audio', ini('coi'), s.text())


def music_player():
    """the Music Player with the packs chosen: arranged title, Tab to the original and back, a boss theme from the pack,
    then QuackShot's"""
    s = S(40).tap('down', gap=40).tap('enter', gap=80).cmd('record v11_mp').wait(20)
    s.tap('enter', gap=300).tap('tab', gap=270).tap('tab', gap=200)                         # title: MD+, original, MD+
    s.tap('up', 4, gap=12).wait(30).tap('enter', gap=330)                                   # Boss - Jack In The Box
    s.tap('right', gap=90).tap('enter', gap=330).cmd('stoprec')                             # QuackShot: Title (MD+)
    return launch('v11_mp', ini('coi', audio={'soundtrack_coi': PACK['coi'], 'soundtrack_qs': PACK['qs']}), s.text())


def music(name, game, index, seconds=60, audio=None, tab=False):
    s = S(40).tap('down', gap=40).tap('enter', gap=60).tap('down', index, gap=10).wait(20)
    if tab: s.tap('tab', gap=20)
    s.cmd('record ' + name).tap('enter', gap=0).wait(int(seconds * 60)).cmd('stoprec')
    return launch(name, ini(game, audio=audio), s.text())


REC = {
    'v11_tree':        lambda: gameplay('v11_tree'),
    'v11_tree_v10':    lambda: gameplay('v11_tree', EXE_V10, 'v11_tree_v10'),
    'v11_toy':         lambda: gameplay('v11_toy'),
    'v11_storm':       lambda: gameplay('v11_storm'),
    'v11_worm':        lambda: gameplay('v11_worm'),
    'v11_mizrabel':    lambda: gameplay('v11_mizrabel'),
    'v11_cheats':      lambda: gameplay('v11_cheats'),
    'v11_md_forest':   lambda: (mdplus_state('v11_md_forest', 2), gameplay('v11_md_forest', audio={'soundtrack_coi': PACK['coi']}))[1],
    'v11_md_duckburg': lambda: (mdplus_state('v11_md_duckburg', 3), gameplay('v11_md_duckburg', audio={'soundtrack_qs': PACK['qs']}))[1],
    'v11_welcome':     welcome,
    'v11_achpage':     ach_page,
    'v11_hall1':       lambda: hallway('v11_hall1', 'hall1', 0, 760, awards=[(40, 'coi_timber')]),
    'v11_hall1_v10':   lambda: hallway('v11_hall1_v10', 'hall1', 0, 760, exe=EXE_V10),
    'v11_hall4':       lambda: hallway('v11_hall4', 'hall4', 0, 1150),
    'v11_rainbow':     lambda: hallway('v11_rainbow', 'hall5', 360, 1240),
    'v11_hall0_169':   lambda: hallway('v11_hall0_169', 'hall0', 0, 800, wide=2),
    'v11_locked':      locked,
    'v11_cheats_ui':   cheats_ui,
    'v11_hotkeys':     hotkeys,
    'v11_audio':       audio_page,
    'v11_mp':          music_player,
    'mus_coi_boss':    lambda: music('mus_coi_boss', 'coi', 5),
    'mus_coi_castle':  lambda: music('mus_coi_castle', 'coi', 1),
    'mus_coi_toyland': lambda: music('mus_coi_toyland', 'coi', 8),
    'mus_md_coi_title': lambda: music('mus_md_coi_title', 'coi', 0, audio={'soundtrack_coi': PACK['coi']}),
    'mus_md_coi_forest': lambda: music('mus_md_coi_forest', 'coi', 2, audio={'soundtrack_coi': PACK['coi']}),
    'mus_md_qs_duckburg': lambda: music('mus_md_qs_duckburg', 'qs', 3, audio={'soundtrack_qs': PACK['qs']}),
}


if __name__ == '__main__':
    names = list(REC) if sys.argv[1:] == ['all'] else sys.argv[1:]
    procs = []
    for name in names:
        procs.append((name, REC[name]()))
        if len(procs) >= 6:
            n0, p0 = procs.pop(0); p0.wait(); print(n0, 'done', p0.returncode, flush=True)
    for name, p in procs:
        p.wait(); print(name, 'done', p.returncode, flush=True)
