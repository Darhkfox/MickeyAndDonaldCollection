"""Trailer helpers: headless sessions that match the app, app-format save states, pad streams.

The app replays what is found here (autotest "loadstate <name>" + "pads <name>", see src/app/app.cpp), so a
headless run and the app's recording are the same frames. Footage folder: trailer_work/cap.
"""
import os, sys, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
WORK = os.path.join(ROOT, 'trailer_work')
CAP = os.path.join(WORK, 'cap')
os.environ.setdefault('MDTOOL_DLL', os.path.join(WORK, 'bin', 'mdtool.dll'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from mdt import ModuleMD, COI, QS, WOI, BTN  # noqa: E402

ROMS = {'coi': COI, 'qs': QS, 'woi': WOI}
EXT = {'4:3': 0, '16:10': 20, '16:9': 40, '2:1': 64, '21:9': 108}
WIDE_SETTING = {0: 0, 20: 1, 40: 2, 64: 3, 108: 4}       # settings.ini [video] widescreen=


def session(game, ext, cheats=()):
    """A headless game set up the way the app sets it up (HUD re-anchored, no sprite-per-line limit)."""
    md = ModuleMD(ROMS[game], game)
    md.configure(ext=ext, hud_anchor=True, cheats=list(cheats))
    md.set_ext(ext, sprite_limit=False)
    md.game = game; md.ext = ext; md.cheats = list(cheats)
    return md


def reconf(md):
    """after md.load(): the module's patches/hooks for this width, as Session::load_state does"""
    md.configure(ext=md.ext, hud_anchor=True, cheats=md.cheats)
    md.set_ext(md.ext, sprite_limit=False)


def pad(names):
    return sum(BTN[b] for b in names.split('+') if b) if isinstance(names, str) else names


def fnv(md):
    h = 2166136261
    for b in bytes(md.ram):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def write_state(md, name):
    """app save-state format: core state + module id + id length"""
    gid = md.game.encode()
    with open(os.path.join(CAP, name + '.state'), 'wb') as f:
        f.write(bytes(md.save()) + gid + bytes([len(gid)]))


def write_pads(pads, name):
    """pads: list of p1 or (p1, p2) per frame"""
    with open(os.path.join(CAP, name + '.pads'), 'wb') as f:
        for p in pads:
            p1, p2 = (p, 0) if isinstance(p, int) else p
            f.write(struct.pack('<HH', p1, p2))


def run(md, pads):
    for p in pads:
        p1, p2 = (p, 0) if isinstance(p, int) else p
        md.hold(p1); md.hold2(p2); md.mrun(1)
