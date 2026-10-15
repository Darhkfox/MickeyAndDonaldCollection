"""QuackShot (REV00) level select from a clean power-on.

    import sys; sys.path.insert(0, r'K:\\projects\\castle of illusion\\tools\\re')
    from qs_game_levelselect import start_level, LEVELS
    md = new_md(); start_level(md, 7)      # -> Transylvania 3-1, gameplay running

    python qs_game_levelselect.py [out.png]   # start every entry, save a screenshot grid

Technique (see docs/re_qs_game.md, "Level select"):
  1. power-on -> title -> START -> START/OPTIONS menu -> START -> intro (skipped with START) -> world map.
  2. When the map is idle (mode $02, $FFA344 == 1): write the progression flags ($FFEEE0-$FFEEFF, plus the
     committed copy $FFFCA0-$FFFCBF), plunger type ($FFEE48 + its palette word $FFF594), ammo, and the
     destination location $FFFF8F34.  The map re-reads $8F34 every frame (routine $9F0C).
  3. Press START (the game's own departure: plane flight, music stop) -> mode $03.  Hideout / Island
     launch by themselves when $EEF9 / ($EEFA and $EEFB) are set, exactly like the real game.
  4. On the first frame of mode $03 write the entry point $FFFF8F74 (the map code at $9D72 has just set it
     to 0 or to the location's "return" entry).  Mode $03 -> $04 loads the level from $8F34/$8F74.
  5. Wait for mode $05 (gameplay) and let the level's drop-in intro finish.
  The "END" entry uses the game's own debug hook: $8F40 = $2F makes mode $03 go to the ending (mode $09).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qs_game_common import (new_md, mode, run_until, wait_mode, tap, boot_to_map, grid,  # noqa: E402
                            GAME_MODE, LOCATION, ENTRY)

# ---------------------------------------------------------------------------------------------
# progression flags ($FFEEE0..$FFEEFF, one byte each, $FF = set) - see doc for meanings
F = dict(
    HERO_KEY=0xEEE0, VIKING_KEY=0xEEE1, SPHINX_TEAR=0xEEE2, SCEPTER_RA=0xEEE3, NOTE_RED_PLUNGER=0xEEE4,
    VIKING_DIARY=0xEEE5, BUBBLEGUM=0xEEE7, MEX_DOOR_OPEN=0xEEEC, GREEN_PLUNGER=0xEEED, TIGER_BEATEN=0xEEEE,
    MET_SENORITA=0xEEEF, TRANS_CASTLE_SEEN=0xEEF0, TRANS_WALL_BLOWN=0xEEF1, PALACE_SEEN=0xEEF2,
    TREASURE_MAP=0xEEF3, EGYPT_DOOR_OPEN=0xEEF5, MET_GYRO=0xEEF7, DIARY_RECOVERED=0xEEF8,
    GO_HIDEOUT=0xEEF9, DIARY_READ=0xEEFA, MAP_DIPPED=0xEEFB, VIKING_DOOR_OPEN=0xEEFC,
)
CLEARED = {loc: 0xEEBF + loc for loc in range(1, 8)}      # $EEC0..$EEC6: location "cleared" (plane pickup)

# cumulative presets in canonical story order: (flags, plunger type, bubble-gum ammo)
_P = []


def _preset(name, add, plunger, gum, remove=()):
    prev = set(_P[-1][1]) if _P else set()
    flags = (prev | set(add)) - set(remove)
    _P.append((name, flags, plunger, gum))
    return name


P_NEW = _preset('new game', [], 0, 0)
P_MEX1 = _preset('Mexico 1st visit', [CLEARED[1]], 0, 0)
P_DUCK2 = _preset('Duckburg for Hero Key', [CLEARED[2], F['MET_SENORITA']], 0, 0)
P_MEX2 = _preset('Mexico ruins', [F['HERO_KEY']], 0, 0)
P_DUCK3 = _preset('Duckburg Gyro', [F['MEX_DOOR_OPEN'], F['NOTE_RED_PLUNGER']], 1, 0)
P_TRANS = _preset('Transylvania', [F['BUBBLEGUM'], F['MET_GYRO']], 1, 50)
P_INDIA = _preset('Maharajah', [CLEARED[3], F['TRANS_CASTLE_SEEN'], F['TRANS_WALL_BLOWN'], F['TREASURE_MAP']], 1, 50)
P_EGYPT = _preset('Egypt', [CLEARED[6], F['PALACE_SEEN'], F['TIGER_BEATEN'], F['SPHINX_TEAR']], 1, 50)
P_SOUTH1 = _preset('South Pole (Viking key)', [CLEARED[7], F['EGYPT_DOOR_OPEN'], F['SCEPTER_RA']], 1, 50)
P_VIKING = _preset('Viking ship', [CLEARED[5], F['VIKING_KEY']], 1, 50)
P_SOUTH2 = _preset('South Pole (diary)', [CLEARED[4], F['VIKING_DOOR_OPEN'], F['GREEN_PLUNGER']], 2, 50)
P_HIDE = _preset('Hideout', [F['GO_HIDEOUT']], 2, 50)          # diary was taken by Pete: $EEE5 stays 0
P_ISLAND = _preset('The Island', [F['DIARY_RECOVERED'], F['VIKING_DIARY'], F['DIARY_READ'], F['MAP_DIPPED']], 2, 50,
                   remove=[F['GO_HIDEOUT']])
PRESETS = {p[0]: p for p in _P}

# ---------------------------------------------------------------------------------------------
# Level list = the game's own (unused) debug stage table at ROM $9DA6 (location), $9DD6 (entry),
# $A06C (3-char label).  'C' = cut-in/connecting area, 'B' = boss, 'nX' = numbered sub-rooms.
LOC_NAMES = {1: 'Duckburg', 2: 'Mexico', 3: 'Transylvania', 4: 'Viking Ship', 5: 'South Pole',
             6: 'Maharajah', 7: 'Egypt', 8: 'Hideout', 9: 'The Island'}
LEVELS = [
    # (label, location, entry, preset, description)
    ('1-1', 1, 0, P_NEW, 'Duckburg streets (start)'),
    ('1-2', 1, 1, P_NEW, 'Duckburg room 1'),
    ('1-3', 1, 3, P_DUCK3, "Duckburg room 2 (Gyro's lab area)"),
    ('2-1', 2, 0, P_MEX1, 'Mexico town'),
    ('2-2', 2, 1, P_MEX1, 'Mexico room 1'),
    ('2-C', 2, 2, P_MEX2, 'Mexico room 2 (ruins entrance / Hero-Key door)'),
    ('2-3', 2, 3, P_MEX2, 'Mexico room 3 (Aztec ruins, Goofy)'),
    ('3-1', 3, 0, P_TRANS, 'Transylvania room 0'),
    ('3-2', 3, 1, P_TRANS, 'Transylvania room 1'),
    ('3-3', 3, 2, P_TRANS, 'Transylvania room 2'),
    ('341', 3, 3, P_TRANS, 'Transylvania room 3 (castle 1)'),
    ('342', 3, 4, P_TRANS, 'Transylvania room 4 (castle 2)'),
    ('343', 3, 5, P_TRANS, 'Transylvania room 5 (castle 3)'),
    ('3-B', 3, 6, P_TRANS, 'Transylvania room 6 (Dracula Duck boss)'),
    ('4-1', 4, 0, P_VIKING, 'Viking ship deck'),
    ('4-2', 4, 1, P_VIKING, 'Viking ship room 1'),
    ('4-C', 4, 2, P_VIKING, 'Viking ship room 2'),
    ('4-3', 4, 3, P_VIKING, 'Viking ship room 3'),
    ('4-B', 4, 4, P_VIKING, 'Viking ship room 4 (ghost boss)'),
    ('5-1', 5, 0, P_SOUTH2, 'South Pole room 0'),
    ('5-2', 5, 1, P_SOUTH2, 'South Pole room 1'),
    ('5-3', 5, 2, P_SOUTH2, 'South Pole room 2'),
    ('5-4', 5, 3, P_SOUTH2, 'South Pole room 3 (Pete kidnaps the nephews)'),
    ('6-1', 6, 0, P_INDIA, "Maharajah's palace grounds"),
    ('6-C', 6, 8, P_INDIA, 'Maharajah room 1'),
    ('621', 6, 1, P_INDIA, 'Maharajah palace room 2'),
    ('622', 6, 2, P_INDIA, 'Maharajah palace room 3'),
    ('623', 6, 3, P_INDIA, 'Maharajah palace room 4'),
    ('624', 6, 4, P_INDIA, 'Maharajah palace room 5'),
    ('625', 6, 5, P_INDIA, 'Maharajah palace room 6'),
    ('626', 6, 6, P_INDIA, 'Maharajah palace room 7'),
    ('6-B', 6, 7, P_INDIA, 'Maharajah room 8 (tiger boss)'),
    ('7-1', 7, 0, P_EGYPT, 'Egypt desert / Sphinx'),
    ('7-2', 7, 1, P_EGYPT, 'Egypt pyramid room 1'),
    ('7-3', 7, 2, P_EGYPT, 'Egypt pyramid room 2'),
    ('7-4', 7, 3, P_EGYPT, 'Egypt pyramid room 3'),
    ('7-C', 7, 4, P_EGYPT, 'Egypt pyramid room 4'),
    ('7-5', 7, 5, P_EGYPT, 'Egypt pyramid room 5'),
    ('8-1', 8, 0, P_HIDE, "Pete's hideout room 0"),
    ('8-2', 8, 1, P_HIDE, 'Hideout room 1'),
    ('8-3', 8, 2, P_HIDE, 'Hideout room 2'),
    ('8-4', 8, 3, P_HIDE, 'Hideout room 3'),
    ('8-B', 8, 4, P_HIDE, 'Hideout room 4 (Pete boss)'),
    ('9-1', 9, 0, P_ISLAND, 'Great Duck Treasure island room 0'),
    ('9-2', 9, 1, P_ISLAND, 'Island room 1'),
    ('9-3', 9, 2, P_ISLAND, 'Island room 2'),
    ('9-B', 9, 3, P_ISLAND, 'Island room 3 (final boss)'),
    ('END', 0, 0, P_NEW, 'Ending (debug stage $2F)'),
]

PLUNGER_PAL = [0x02CE, 0x022E, 0x02A2]    # ROM $501E: yellow / red / green plunger colour -> $FFF594
RETURN_ENTRY_TRIGGER = {1: 0, 2: 2, 3: 0, 4: 2, 5: 0, 6: 0, 7: 0}   # ROM $475C (see $46C8)


def apply_preset(md, preset, loc=None, entry=None):
    """Write progression flags / plunger / ammo (working copy $EEE0 and committed copy $FCA0)."""
    name, flags, plunger, gum = PRESETS[preset]
    flags = set(flags)
    if loc in RETURN_ENTRY_TRIGGER and entry == RETURN_ENTRY_TRIGGER[loc]:
        flags.discard(CLEARED[loc])           # else mode 4 ($46C8) swaps in the location's return entry
    for a in range(0xEEC0, 0xEF00):
        md.set8(0xFF0000 | a, 0xFF if a in flags else 0)
    for i in range(0x20):                   # committed copy restored by continue ($5046)
        md.set8(0xFFFCA0 + i, md.ram8(0xFFEEE0 + i))
    md.set8(0xFFEE48, plunger)
    md.set16(0xFFF594, PLUNGER_PAL[plunger]); md.set8(0xFFEE02, 1)    # what $5000 does
    md.set16(0xFF8F92, gum)
    if gum:
        md.set8(0xFFEE5B, md.ram8(0xFFEE5B) | 1)                       # HUD ammo redraw request


def start_level(md, index, from_power_on=True, settle=200):
    """Start LEVELS[index] from power-on. Returns True when gameplay (mode 5) is running."""
    label, loc, entry, preset, _ = LEVELS[index]
    if from_power_on:
        boot_to_map(md)
    assert mode(md) == 0x02 and md.ram16(0xFFA344) == 1, 'world map not idle'
    apply_preset(md, preset, loc, entry)
    if label == 'END':
        md.set16(0xFF8F40, 0x2F)            # debug stage "END": mode 3 -> mode 9 (ending)
        tap(md, 'START', 2, 2)
        return wait_mode(md, 0x09, 600) >= 0
    md.set16(LOCATION, loc)
    md.run(2)                                # map redraws plane marker + name for the new $8F34
    if mode(md) == 0x02 and md.ram16(0xFFA344) == 1 and loc not in (8, 9):
        tap(md, 'START', 2, 1)               # normal departure (loc 8/9 auto-launch from the flags)
    if run_until(md, lambda d: mode(d) == 0x03, 900) < 0:
        return False
    md.set16(ENTRY, entry)
    md.set16(LOCATION, loc)
    if wait_mode(md, 0x05, 900) < 0:
        return False
    md.run(settle)
    return mode(md) == 0x05


def playable(md, frames=40):
    """Crude playability check: Donald's X ($FFA2D0) changes while RIGHT (then LEFT) is held."""
    x0 = md.ram16(0xFFA2D0)
    md.hold('RIGHT'); md.run(frames); md.hold(0)
    x1 = md.ram16(0xFFA2D0)
    if x1 == x0:
        md.hold('LEFT'); md.run(frames); md.hold(0)
        x1 = md.ram16(0xFFA2D0)
    return x1 != x0


def verify_mexico_gate(md=None):
    """End-to-end story-gate check: 2-C with the 'Mexico ruins' preset.  The senorita now says msg 7
    ('use the Hero Key'), Donald USEs the Hero Key from the pause menu at the ruins door ($EED6 -> $EED0 ->
    door opens, $EEEC set) and walks through it into room 3 (Aztec ruins, music $8B)."""
    md = md or new_md()
    assert start_level(md, 5)
    md.hold('RIGHT'); md.run(150); md.hold(0); md.run(60)        # senorita dialogue opens
    md.press('C', 4, 4)                                              # close it (location-cleared cut-scene)
    run_until(md, lambda d: d.ram8(0xFFEE5E) == 0, 1200); md.run(30)
    md.hold('RIGHT'); run_until(md, lambda d: d.ram8(0xFFEED6) != 0, 400); md.hold(0); md.run(10)
    md.press('START', 4, 60); md.press('DOWN', 4, 20); md.press('C', 4, 30); md.press('C', 4, 60)  # USE key
    used = md.ram8(0xFFEED0)
    for _ in range(6):
        if mode(md) == 0x05:
            break
        md.press('C', 4, 40)
    md.run(200)
    door = md.ram8(0xFFEEEC)
    md.hold('LEFT'); run_until(md, lambda d: d.ram16(0xFFA2D0) <= 452, 200); md.hold(0); md.run(20)
    md.hold('UP'); md.run(30); md.hold(0); md.run(120)
    res = dict(key_used=hex(used), door_open=hex(door), room=md.ram16(0xFF8F36), music=hex(md.ram8(0xFFEE67)))
    return used == 0xFF and door == 0xFF and res['room'] == 3, res


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'qs_game_levelselect.png')
    import tempfile
    tmp = tempfile.mkdtemp(prefix='qs_game_ls_')
    paths, labels = [], []
    for i, (label, loc, entry, preset, desc) in enumerate(LEVELS):
        md = new_md()
        ok = start_level(md, i)
        if label == 'END':
            md.run(300)
            info = 'mode=%02X' % mode(md)
            mv = ok
        else:
            info = 'loc=%d entry=%d room=%d mode=%02X x=%d y=%d' % (
                md.ram16(LOCATION), md.ram16(ENTRY), md.ram16(0xFF8F36), mode(md),
                md.ram16(0xFFA2D0), md.ram16(0xFFA2D4))
            mv = playable(md) if ok else False
        p = os.path.join(tmp, '%02d.png' % i)
        md.png(p, 1)
        paths.append(p)
        labels.append('%s %s %s%s' % (label, LOC_NAMES.get(loc, 'END'), 'OK' if ok else 'FAIL',
                                      '' if mv else ' (no move)'))
        print('%2d %-4s %-40s %-26s ok=%s moves=%s %s' % (i, label, desc, preset, ok, mv, info), flush=True)
    grid(paths, out, cols=6, labels=labels)
    print('grid ->', out)
    print('story gate (Mexico Hero-Key door):', verify_mexico_gate())
