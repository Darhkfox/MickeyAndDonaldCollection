"""Trailer footage of the app's own screens (launcher, menus, music player) and the music beds.
  python ui.py <name> [...]      names: see UI below
Recorded like record.py (hidden 1920x1080 autotest window, one exe copy per recording).
Menu shots that need a game running behind them load a bot clip's start state first.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from record import launch, settings_ini  # noqa: E402

GAP = 14


class S:
    def __init__(self, start=40):
        self.f = start; self.lines = []
    def wait(self, n): self.f += n; return self
    def tap(self, key, n=1, gap=GAP):
        for _ in range(n):
            self.lines.append('%d tap %s' % (self.f, key)); self.f += gap
        return self
    def cmd(self, text): self.lines.append('%d %s' % (self.f, text)); return self
    def down(self, key): return self.cmd('down ' + key)
    def up(self, key): return self.cmd('up ' + key)
    def text(self, end_pad=5):
        return '\n'.join(self.lines + ['%d quit' % (self.f + end_pad)]) + '\n'


PAUSE = {'coi': ['Resume', 'Slot', 'Save State', 'Load State', 'Level Select', 'Cheats', 'Sound Test', 'Music Player',
                 'Options', 'Credits', 'Reset Game', 'Return to Launcher', 'Quit to Desktop']}
PAUSE['qs'] = PAUSE['coi']
PAUSE['woi'] = PAUSE['coi'][:8] + ['Player 2 Controls'] + PAUSE['coi'][8:]


def in_game(s, state, settle=60):
    """from the launcher (last_game preselected): start the game and load a bot clip's start state"""
    s.tap('enter').wait(90).cmd('loadstate ' + state).wait(settle)
    return s


def pause_to(s, game, item):
    s.tap('esc', gap=30)
    s.tap('down', PAUSE[game].index(item), gap=10)
    return s


def launcher():
    """the attract-mode previews need ~50 s to get past the logos and story screens"""
    s = S(30).wait(2900).cmd('record ui_launcher')
    for k in ['right', 'right', 'left', 'left', 'right', 'right', 'left', 'left', 'right', 'right']:
        s.wait(300).tap(k, gap=0)
    s.wait(300).tap('down', gap=90).tap('left', gap=90).tap('right', gap=90).tap('right', gap=90).tap('up', gap=200)
    return 'coi', {}, s.cmd('stoprec').text()


def music(name, game, index, seconds=95):
    s = S(40).tap('down', gap=40).tap('enter', gap=60).tap('down', index, gap=10).wait(20)
    s.cmd('record ' + name).tap('enter', gap=0).wait(int(seconds * 60)).cmd('stoprec')
    return game, {}, s.text()


def level_select(name, game, state, rows, var_rights=0):
    s = in_game(S(), state).cmd('record ' + name).wait(30)
    pause_to(s, game, 'Level Select').wait(20).tap('enter', gap=40)
    if game == 'woi': s.tap('right', var_rights, gap=40)
    s.tap('down', rows, gap=9).wait(40).tap('enter', gap=40).tap('up', gap=30).tap('enter', gap=0)
    s.wait(360).cmd('stoprec')
    return game, {}, s.text()


def cheats(name, game, state, toggles):
    s = in_game(S(), state).cmd('record ' + name).wait(30)
    pause_to(s, game, 'Cheats').wait(20).tap('enter', gap=40)
    for d in toggles:
        s.tap('down', d, gap=12).wait(8).tap('enter', gap=30)
    s.wait(60).tap('esc', gap=30).tap('esc', gap=60).cmd('stoprec')
    return game, {}, s.text()


def states(name, game, state):
    s = in_game(S(), state).cmd('record ' + name).wait(60)
    pause_to(s, game, 'Save State').wait(20).tap('enter', gap=150)
    s.tap('f9', gap=150).cmd('stoprec')
    return game, {}, s.text()


def video_options(name, game, state):
    s = in_game(S(), state).cmd('record ' + name).wait(30)
    pause_to(s, game, 'Options').wait(20).tap('enter', gap=40).tap('enter', gap=40)   # Options -> Video
    s.tap('down', 3, gap=12).wait(30)                                                  # Widescreen row
    s.tap('right', 4, gap=80).wait(40)                                                 # 4:3 -> 16:10 -> 16:9 -> 2:1 -> 21:9
    s.tap('esc', gap=30).tap('down', gap=20).tap('enter', gap=40)                     # Colour page
    s.tap('right', 7, gap=55).tap('right', gap=60).wait(30)
    s.tap('esc', gap=30).tap('esc', gap=30).tap('esc', gap=60).cmd('stoprec')
    return game, {'widescreen': 0}, s.text()


def controls(name):
    s = in_game(S(), 'woi_forest2').cmd('record ' + name).wait(30)
    pause_to(s, 'woi', 'Player 2 Controls').wait(20).tap('enter', gap=200).tap('esc', gap=30).tap('esc', gap=40)
    s.cmd('stoprec')
    return 'woi', {}, s.text()


UI = {
    'ui_launcher':      launcher,
    'mus_woi_title':    lambda: music('mus_woi_title', 'woi', 0),
    'mus_coi_forest':   lambda: music('mus_coi_forest', 'coi', 2),
    'mus_qs_duckburg':  lambda: music('mus_qs_duckburg', 'qs', 3),
    'mus_woi_forest':   lambda: music('mus_woi_forest', 'woi', 3),
    'mus_coi_title':    lambda: music('mus_coi_title', 'coi', 0),
    'mus_qs_title':     lambda: music('mus_qs_title', 'qs', 0),
    'mus_woi_sky':      lambda: music('mus_woi_sky', 'woi', 15),
    'ls_coi':           lambda: level_select('ls_coi', 'coi', 'coi_forest', 9),
    'ls_qs':            lambda: level_select('ls_qs', 'qs', 'qs_duckburg', 32),
    'ls_woi':           lambda: level_select('ls_woi', 'woi', 'woi_forest2', 12, var_rights=2),
    'ch_coi':           lambda: cheats('ch_coi', 'coi', 'coi_forest', [0, 1, 1]),
    'ch_qs':            lambda: cheats('ch_qs', 'qs', 'qs_duckburg', [0, 2, 3]),
    'ch_woi':           lambda: cheats('ch_woi', 'woi', 'woi_forest2', [0, 1]),
    'st_qs':            lambda: states('st_qs', 'qs', 'qs_egypt'),
    'opt_video':        lambda: video_options('opt_video', 'qs', 'qs_ultra'),
    'ctl_woi':          lambda: controls('ctl_woi'),
}


if __name__ == '__main__':
    procs = []
    for name in sys.argv[1:]:
        game, video, text = UI[name]()
        procs.append((name, launch(name, settings_ini(game, video), text)))
    for name, p in procs:
        p.wait(); print(name, 'done', flush=True)
