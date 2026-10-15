"""Castle of Illusion cheats: RAM map, per-frame RAM cheats (with HUD refresh) and ROM patches.
See docs/re_coi_game.md section 3.  `python coi_game_cheats.py` runs the in-game verification.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coi_game_util import MD, COI, shot, grid  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
STATES = os.path.join(os.path.dirname(HERE), 'states')

# ------------------------------------------------------------------ RAM (68000 addresses)
SCORE = 0xFFF22C        # long, plain binary (NOT BCD); HUD redraws itself when it changes
SCORE_SHOWN = 0xFFF23C  # long, last score drawn on the HUD
NEXT_1UP = 0xFFF282     # long, score for next extra try: 40000 first, then +50000 each
ITEMS = 0xFFF320        # word, apples/marbles/... "ITEMS" 0..30 ($1E)
POWER = 0xFFF322        # word, health stars 0..5 (HUD shows 5 slots)
TRIES = 0xFFF324        # word, lives shown as "TRIES xNN", 0..10 (0 = last life)
INVULN = 0xFFF446       # byte, post-hit invulnerability/blink timer ($5B after a hit, $52 on death)
HUD_FLAGS = 0xFFF409    # byte, bit0 power, bit1 tries, bit2 items, bit3 score -> redraw request
HUD_TRIES_TILES = 0xFFF234   # long, 2 name-table words (tens, ones) for TRIES
HUD_ITEMS_TILES = 0xFFF238   # long, 2 words for ITEMS
HUD_POWER_TILES = 0xFFF244   # 5 words, $807C = full star, $807D = empty
CONTINUES = 0xFFFF40    # byte, continues left (Normal 3, Hard 1, Practice 5)
DIFFICULTY = 0xFFFF0F   # byte, 0 Normal, 1 Hard, 2 Practice (edited copy at $FFFF0D)
MODE = 0xFFF300
MICKEY = 0xFFDC00       # Mickey's object slot ($40 bytes): +$10 X, +$14 Y (words), +$31 flags

# ------------------------------------------------------------------ ROM patches
# (address, original word, patched word, description)
PATCHES = {
    'infinite_power': [
        (0x0092F4, 0x5378, 0x4E71, 'SUBQ.W #1,$F322 -> NOP (hit costs no power)'),
        (0x0092F6, 0xF322, 0x4E71, '  (second word of the SUBQ)'),
    ],
    'invincible': [   # no damage from enemies/projectiles/spikes at all (no knock-back either)
        (0x0092B4, 0x4A38, 0x4E75, 'damage check A: TST.B $F446 -> RTS (returns d7=0 "no hit")'),
        (0x0092D4, 0x4A38, 0x4E75, 'damage check B: TST.B $F446 -> RTS (returns d7=0 "no hit")'),
        (0x00B526, 0x6604, 0x6004, 'direct damage #1: BNE -> BRA (skip JMP $92EE)'),
        (0x00BF74, 0x6604, 0x6004, 'spike/hazard damage: BNE -> BRA (skip JSR $92EE, bounce kept)'),
    ],
    'infinite_tries': [
        (0x009300, 0x5378, 0x4E71, 'death by power 0: SUBQ.W #1,$F324 -> NOP'),
        (0x009302, 0xF324, 0x4E71, ''),
        (0x00BD6E, 0x5378, 0x4E71, 'pit / kill-tile death: SUBQ.W #1,$F324 -> NOP'),
        (0x00BD70, 0xF324, 0x4E71, ''),
    ],
    'infinite_items': [
        (0x00B8CA, 0x5378, 0x4E71, 'throw: SUBQ.W #1,$F320 -> NOP'),
        (0x00B8CC, 0xF320, 0x4E71, ''),
    ],
    'infinite_continues': [
        (0x07A590, 0x5338, 0x4E71, 'game over: SUBQ.B #1,$FF40 -> NOP'),
        (0x07A592, 0xFF40, 0x4E71, ''),
    ],
    'max_power_start': [   # every (re)start of a level with 5 stars instead of 3 (Normal) / 2 (Hard)
        (0x00972A, 0x7003, 0x7005, 'MOVEQ #3,D0 -> MOVEQ #5,D0 (Normal)'),
        (0x009746, 0x0002, 0x0005, 'MOVE.W #2,$F322 -> #5 (Hard)'),
    ],
    'debug_level_select': [   # options screen: A/B change level, label under CONTROL, START plays
        (0x009F6C, 0x4E75, 0x4E71, 'RTS at end of options handler -> NOP, falls into $9F6E'),
    ],
}


CHECKSUM = 0x00018E    # header word = sum of all words $000200..$07FFFF, verified at cold boot


def apply_patch(md, name, fix_checksum=True):
    """Patch the loaded ROM.  The cold-boot code ($000324) sums $200..$7FFFF and, on mismatch with
    the header word at $18E, jumps to $4CE (red screen, dead loop) - so a patch applied before
    power-on must also correct $18E (fix_checksum), or be applied after the boot check has run."""
    rom = md.rom
    for a, orig, new, _ in PATCHES[name]:
        cur = (rom[a] << 8) | rom[a + 1]
        if cur not in (orig, new):
            raise RuntimeError('%s: ROM $%06X = %04X, expected %04X' % (name, a, cur, orig))
        if cur == new:
            continue
        md.patch16(a, new)
        if fix_checksum:
            ck = (rom[CHECKSUM] << 8) | rom[CHECKSUM + 1]
            md.patch16(CHECKSUM, (ck + new - orig) & 0xFFFF)


# ------------------------------------------------------------------ RAM cheats with HUD refresh
def _two_digit_tiles(v):
    """Same as $9C06: (tens,ones) -> ($8010+tens)<<16 | ($8010+ones)."""
    return ((0x8010 + v // 10) << 16) | (0x8010 + v % 10)


def set_power(md, v):
    v = max(0, min(5, v))
    md.set16(POWER, v)
    for i in range(5):
        md.set16(HUD_POWER_TILES + 2 * i, 0x807C if i < v else 0x807D)
    md.set8(HUD_FLAGS, md.ram8(HUD_FLAGS) | 1)


def set_tries(md, v):
    v = max(0, min(10, v))
    md.set16(TRIES, v)
    md.set32(HUD_TRIES_TILES, _two_digit_tiles(v))
    md.set8(HUD_FLAGS, md.ram8(HUD_FLAGS) | 2)


def set_items(md, v):
    v = max(0, min(30, v))
    md.set16(ITEMS, v)
    md.set32(HUD_ITEMS_TILES, _two_digit_tiles(v))
    md.set8(HUD_FLAGS, md.ram8(HUD_FLAGS) | 4)


def set_score(md, v, award_1ups=False):
    """Score is a plain 32-bit binary number; $9C42 redraws the HUD when it differs from $F23C and
    awards one try per threshold crossed ($F282: 40000, then +50000).  Unless award_1ups, move the
    threshold past the new score so poking the score does not hand out tries."""
    md.set32(SCORE, v)
    if not award_1ups:
        nxt = 40000
        while nxt <= v:
            nxt += 50000
        md.set32(NEXT_1UP, nxt)


def ram_cheats(md, power=None, tries=None, items=None, invincible=False):
    """Per-frame freeze.  Call once per frame while mode ($FFF300) == $0E (gameplay).
    Only writes when the value differs, so the HUD is refreshed once per change."""
    if md.ram16(MODE) != 0x0E:
        return
    if power is not None and md.ram16(POWER) != power:
        set_power(md, power)
    if tries is not None and md.ram16(TRIES) != tries:
        set_tries(md, tries)
    if items is not None and md.ram16(ITEMS) != items:
        set_items(md, items)
    if invincible:
        md.set8(INVULN, 3)      # odd and >1: all 4 damage entry points skip, sprite stays visible


# ------------------------------------------------------------------ verification
def _load(md, lvl):
    md.load(open(os.path.join(STATES, 'coi_game_lvl%02d.state' % lvl), 'rb').read())


def test_enemy_hit(label, patch=None, ram=None, frames=300):
    """Level 1-3 (index 2): walking right runs into a walking tree after ~1.5 s."""
    md = MD(COI)
    if patch:
        apply_patch(md, patch)
    _load(md, 2)
    md.watch(INVULN, INVULN)
    hit_frame = None
    p0 = md.ram16(POWER)
    for f in range(frames):
        if ram:
            ram_cheats(md, **ram)
        md.run(1, 'RIGHT')
        if hit_frame is None and md.ram16(POWER) != p0:
            hit_frame = f
    md.hold(0)
    hits = [h for h in md.watch_hits() if h['value'] in (0x5B,)]
    im = shot(md)
    r = dict(label=label, power_before=p0, power_after=md.ram16(POWER),
             invuln_set_by=sorted({hex(h['pc']) for h in hits}), x=md.ram16(MICKEY + 0x10))
    print('%-28s power %d -> %d   $F446=$5B written by %s   Mickey X=%d' % (
        label, r['power_before'], r['power_after'], r['invuln_set_by'] or '-', r['x']))
    return r, im


PIT_INPUT = [('RIGHT+C' if (f % 40) < 12 else 'RIGHT') for f in range(400)]


def test_pit(label, patch=None, ram=None):
    """Level 1-3 (index 2): holding RIGHT with jumps falls into the pit at ~frame 270."""
    md = MD(COI)
    if patch:
        apply_patch(md, patch)
    _load(md, 2)
    t0 = md.ram16(TRIES)
    md.watch(TRIES, TRIES + 1)
    for f, b in enumerate(PIT_INPUT):
        md.set16(POWER, 5)        # keep enemies from killing Mickey first (tries untouched)
        if ram:
            ram_cheats(md, **ram)
        md.run(1, b)
    md.hold(0)
    seq = []
    for f in range(500):
        if ram:
            ram_cheats(md, **ram)
        md.run(1)
        m = md.ram16(MODE)
        if not seq or seq[-1] != m:
            seq.append(m)
        if m == 0x0E and len(seq) > 2:
            break
    md.run(60)
    wr = [(hex(h['pc']), h['value']) for h in md.watch_hits() if h['addr'] in (TRIES, TRIES + 1)]
    im = shot(md)
    print('%-28s tries %d -> %d  tries writes %s  modes %s  level %d' % (
        label, t0, md.ram16(TRIES), wr[:3], [hex(x) for x in seq], md.ram16(0xFFFF42)))
    return im


def test_items(label, patch=None):
    md = MD(COI)
    if patch:
        apply_patch(md, patch)
    _load(md, 0)
    set_items(md, 5)
    md.run(2)
    for i in range(4):
        md.press('B', 2, 30)
    im = shot(md)
    print('%-28s items 5 -> %d' % (label, md.ram16(ITEMS)))
    return im


def test_game_over(label, patch=None):
    """Last try (TRIES=1) + pit -> game over (mode $18): continues $FFFF40 before/after."""
    md = MD(COI)
    if patch:
        apply_patch(md, patch)
    _load(md, 2)
    set_tries(md, 1)
    c0 = md.ram8(CONTINUES)
    for b in PIT_INPUT:
        md.run(1, b)
    md.hold(0)
    modes = []
    for f in range(400):
        md.run(1)
        m = md.ram16(MODE)
        if not modes or modes[-1] != m:
            modes.append(m)
    im = shot(md)
    print('%-28s continues %d -> %d  modes %s' % (label, c0, md.ram8(CONTINUES), [hex(x) for x in modes]))
    return im


def test_max_power_start():
    from coi_game_levelselect import start_level
    md = MD(COI)
    apply_patch(md, 'max_power_start')
    start_level(md, 5, hallway=False)
    md.run(60)
    print('%-28s power at start of 2-1 = %d' % ('max_power_start patch', md.ram16(POWER)))
    return shot(md)


def test_hud_refresh():
    md = MD(COI)
    _load(md, 0)
    set_power(md, 5); set_tries(md, 9); set_items(md, 30); set_score(md, 123450)
    md.run(3)
    print('%-28s power=%d tries=%d items=%d score=%d (see HUD in grid)' % (
        'RAM set + HUD refresh', md.ram16(POWER), md.ram16(TRIES), md.ram16(ITEMS), md.ram32(SCORE)))
    return shot(md)


def test_debug_level_select():
    """ROM patch $9F6C from power-on (checksum fixed): press B five times on OPTIONS, START."""
    from coi_game_levelselect import to_options, press_until
    md = MD(COI)
    apply_patch(md, 'debug_level_select')
    md.reset()
    to_options(md)
    for i in range(5):
        lv = md.ram16(0xFFFF42)
        press_until(md, 'B', lambda: md.ram16(0xFFFF42) != lv, 3, 'level +1')
    md.run(10)
    im1 = shot(md)
    press_until(md, 'START', lambda: md.ram16(MODE) == 0x0C, 3, 'start')
    for _ in range(600):
        md.run(1)
        if md.ram16(MODE) == 0x0E:
            break
    md.run(90)
    print('%-28s options label shows level %d; START -> mode %X level %d round %d' % (
        'debug level select patch', 5, md.ram16(MODE), md.ram16(0xFFFF42), md.ram16(0xFFFF44)))
    return im1, shot(md)


def main():
    ims, labs = [], []

    def add(im, lab):
        ims.append(im); labs.append(lab)

    r, im = test_enemy_hit('baseline enemy hit'); add(im, 'baseline hit')
    r, im = test_enemy_hit('ROM infinite_power', patch='infinite_power'); add(im, 'inf power (ROM)')
    r, im = test_enemy_hit('ROM invincible', patch='invincible'); add(im, 'invincible (ROM)')
    r, im = test_enemy_hit('RAM invincible ($F446=3)', ram=dict(invincible=True)); add(im, 'invincible (RAM)')
    r, im = test_enemy_hit('RAM power freeze 5', ram=dict(power=5)); add(im, 'power=5 (RAM)')
    add(test_pit('baseline pit'), 'baseline pit')
    add(test_pit('ROM infinite_tries pit', patch='infinite_tries'), 'inf tries (ROM) pit')
    add(test_pit('RAM tries freeze 3 pit', ram=dict(tries=3)), 'tries=3 (RAM) pit')
    add(test_pit('ROM invincible pit', patch='invincible'), 'invincible (ROM) pit')
    add(test_items('baseline items'), 'baseline items')
    add(test_items('ROM infinite_items', patch='infinite_items'), 'inf items (ROM)')
    add(test_game_over('baseline game over'), 'game over')
    add(test_game_over('ROM infinite_continues', patch='infinite_continues'), 'inf continues')
    add(test_max_power_start(), 'max power start')
    add(test_hud_refresh(), 'HUD refresh')
    a, b = test_debug_level_select()
    add(a, 'debug lvl select: options'); add(b, 'debug lvl select: 2-1')
    out = os.path.join(HERE, 'coi_game_cheats_grid.png')
    grid(ims, 5, labs, out)
    print('grid ->', out)


if __name__ == '__main__':
    main()
