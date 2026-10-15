"""Castle of Illusion level select (see docs/re_coi_game.md section 2).

    from coi_game_levelselect import start_level, LEVELS
    md = MD(COI)                 # fresh power-on
    start_level(md, 13)          # Library 4-1-1, entered through the castle hallway like the game does

Technique (all from a clean power-on, deterministic):
  1. run until the main-loop mode $FFF300 == $06 (opening story), press START  -> mode $12 (title)
  2. press START on the title                                               -> $1C -> $08 -> $0A (OPTIONS)
  3. on the options screen (mode $0A): write level index to $FFFF42 (word) [optionally the
     difficulty to $FFFF0D], then press START.  The game's own START handler ($9FCC) derives
     round/act ($FFFF44/$FFFF46, routine $9FF0), copies difficulty to $FFFF0F, initialises the
     continues ($A094) and sets mode $0C (level init) - or $28 (old-man story) for level 0.
     This is exactly the path of the game's hidden debug level select (see LEVELSELECT_PATCH).
  4. round-first levels (0,5,9,13,23,27) are normally entered through the castle hallway cut-scene
     (mode $20/$22, "WHAT KIND OF ROOM IS BEHIND THIS DOOR?", music $9E).  With hallway=True the
     mode word written by the START handler is replaced by $20 before the next main-loop pass, so
     the hallway runs and then hands over to mode $0C itself, as after a boss tally.
  5. mode $0C loads the level (graphics, palette, music from table $96B0, HUD) and on this first
     init ($FFF415 bit0 clear) sets tries $FFF324=3, power $FFF322=3/2/5 (Normal/Hard/Practice),
     next-1UP score $FFF282=40000.  Then mode $0E = gameplay.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coi_game_util import MD, COI, shot, grid  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
STATES = os.path.join(os.path.dirname(HERE), 'states')

MODE = 0xFFF300
LEVEL = 0xFFFF42
ROUND = 0xFFFF44
ACT = 0xFFFF46
DIFF_EDIT = 0xFFFF0D     # difficulty as edited on the options screen
DIFF = 0xFFFF0F          # difficulty used by the game (copied from $FF0D on options exit)

# index ($FFFF42), debug label (ROM table $A040), description.  Play order in docs.
LEVELS = [
    (0x00, '1-1-0', 'Enchanted Forest - Act 1, forest floor (start of game)'),
    (0x01, '1-2-0', 'Enchanted Forest - Act 2, tree tops / apple trees'),
    (0x02, '1-3-0', 'Enchanted Forest - Act 1, part 2 (walking trees)'),
    (0x03, '1-4-0', 'Enchanted Forest - Act 1, part 3 (spider-web cavern)'),
    (0x04, '1-B-0', 'Enchanted Forest - Boss (evil tree)'),
    (0x05, '2-1-0', 'Toyland - Act 1'),
    (0x06, '2-2-0', 'Toyland - Act 2'),
    (0x07, '2-3-0', 'Toyland - Act 3 (toy soldiers / block castle)'),
    (0x08, '2-B-0', 'Toyland - Boss'),
    (0x09, '3-1-0', 'The Storm - Act 1 (storm cliffs, waterfall cave)'),
    (0x0A, '3-2-0', 'The Storm - Act 2 (ruins)'),
    (0x0B, '3-3-0', 'The Storm - Act 3 (ruins)'),
    (0x0C, '3-B-0', 'The Storm - Boss'),
    (0x0D, '4-1-1', 'Library/Dessert - Act 1 shelves, part 1 (start, doors)'),
    (0x0E, '4-1-2', 'Library/Dessert - Act 1 shelves, part 2 (milk bottle)'),
    (0x0F, '4-1-3', 'Library/Dessert - shelves after tea cup 1'),
    (0x10, '4-1-4', 'Library/Dessert - shelves after tea cup 2'),
    (0x11, '4-1-5', 'Library/Dessert - shelves after tea cup 3'),
    (0x12, '4-T-1', 'Library/Dessert - inside tea cup 1 (swimming)'),
    (0x13, '4-T-2', 'Library/Dessert - inside tea cup 2 (swimming)'),
    (0x14, '4-T-3', 'Library/Dessert - inside tea cup 3 (swimming)'),
    (0x15, '4-2-0', 'Library/Dessert - Act 2, dessert land (cakes)'),
    (0x16, '4-B-0', 'Library/Dessert - Boss'),
    (0x17, '5-1-0', 'Clock Tower - Act 1 (castle hall)'),
    (0x18, '5-S-0', 'Clock Tower - Act 1 shaft/stairs'),
    (0x19, '5-2-0', 'Clock Tower - Act 2 (gears)'),
    (0x1A, '5-B-0', 'Clock Tower - Boss'),
    (0x1B, 'L-B-0', 'Castle of Illusion - final battle with Mizrabel'),
]
ROUND_FIRST = {0x00, 0x05, 0x09, 0x0D, 0x17, 0x1B}   # entered via the castle hallway
BOSSES = {0x04, 0x08, 0x0C, 0x16, 0x1A, 0x1B}

# ROM patch that re-enables the developers' level select on the OPTIONS screen:
# A = previous level, B = next level, label "R-A-x" drawn under CONTROL; START starts it.
# ($9F6C is the RTS that ends the options handler; with a NOP it falls into the orphaned
#  routine $9F6E.)  Note B also decrements the highlighted option value.
LEVELSELECT_PATCH = (0x009F6C, 0x4E75, 0x4E71)


def _run_until(md, cond, maxf, what):
    for _ in range(maxf):
        if cond():
            return
        md.run(1)
    raise RuntimeError('timeout waiting for ' + what)


def press_until(md, button, cond, maxf, what, tries=20):
    """Tap `button` until cond() becomes true (edge-triggered menus), return with it released.

    Menus test the 'newly pressed' byte $FFF309, which the VBlank pad reader ($AE4E) computes
    against the previous held byte $FFF308.  Mode inits write $FF00 there ($95CA) and while a
    screen is being built the main loop is busy, so an edge can be missed: wait until $FFF308
    reads back 0, hold for up to `maxf` frames, release, retry."""
    for _ in range(tries):
        md.hold(0)
        _run_until(md, lambda: md.ram8(0xFFF308) == 0, 600, 'pad idle')
        md.hold(button)
        for _ in range(maxf):
            md.run(1)
            if cond():
                md.hold(0)
                return
        md.hold(0)
        md.run(1)
    raise RuntimeError('timeout waiting for ' + what)


def to_options(md):
    """From power-on (or anywhere in the attract loop) to the OPTIONS screen (mode $0A)."""
    _run_until(md, lambda: md.ram16(MODE) == 0x06, 2000, 'opening story (mode 6)')
    press_until(md, 'START', lambda: md.ram16(MODE) == 0x12, 3, 'title (mode $12)')
    press_until(md, 'START', lambda: md.ram16(MODE) != 0x12, 3, 'leaving title')
    _run_until(md, lambda: md.ram16(MODE) == 0x0A, 300, 'options (mode $0A)')
    md.run(2)


def start_level(md, index, hallway=True, story=False, difficulty=None, power_on=True,
                wait_gameplay=True):
    """Start level `index` (0..27) the way the game does.  Returns frames spent.

    hallway:   round-first levels go through the castle hallway cut-scene (as in the real game)
    story:     for level 0 also play the old-man story (mode $28, START-skippable in-game)
    difficulty: None = leave options default (0 Normal), or 0 Normal / 1 Hard / 2 Practice
    """
    if not 0 <= index <= 0x1B:
        raise ValueError('level index 0..27')
    if power_on:
        md.reset()
    f0 = md.frame
    to_options(md)
    md.set16(LEVEL, index)
    if difficulty is not None:
        md.set8(DIFF_EDIT, difficulty)
    # START handler $9FCC runs on the first frame START is seen pressed; stop right after it
    press_until(md, 'START', lambda: md.ram16(MODE) in (0x0C, 0x28), 3, 'START handler')
    if index == 0 and story:
        pass                          # mode $28 old-man story -> $20 hallway -> $0C
    elif index in ROUND_FIRST and hallway:
        md.set16(MODE, 0x20)          # hallway cut-scene, it sets mode $0C when Mickey enters
    else:
        md.set16(MODE, 0x0C)          # (level 0 without hallway/story: straight to level init)
    if wait_gameplay:
        _run_until(md, lambda: md.ram16(MODE) == 0x0E and md.ram16(LEVEL) == index, 4000,
                   'gameplay')
    return md.frame - f0


def warp_level(md, index, hallway=True, wait_gameplay=True):
    """Jump to a level from a running game (any mode after the options screen, e.g. gameplay).

    mode $0C directly: just set $FFFF42 and $FFF300=$0C (round/act are re-derived by level init).
    hallway: the hallway init does not clear leftover level objects, so zero the object RAM
    $FFD000-$FFDFFF first (else the hallway Mickey never reaches the door), set $FFFF44 (round,
    it selects the hallway variant) and $FFF300=$20.  Tries/power/score/items are kept (the level
    init only tops power up to the difficulty minimum after a death/round clear)."""
    rnd = [r for r, lo in enumerate((0x00, 0x05, 0x09, 0x0D, 0x17, 0x1B)) if index >= lo][-1]
    md.set16(LEVEL, index)
    md.set16(0xFFF340, 0)                 # no palette fade pending
    if hallway and index in ROUND_FIRST:
        for a in range(0xD000, 0xE000):
            md.set8(0xFF0000 | a, 0)
        md.set16(ROUND, rnd)
        md.set16(MODE, 0x20)
    else:
        md.set16(MODE, 0x0C)
    if wait_gameplay:
        _run_until(md, lambda: md.ram16(MODE) == 0x0E and md.ram16(LEVEL) == index, 4000,
                   'gameplay')


def main():
    out = os.path.join(HERE, 'coi_game_levelselect_grid.png')
    save_states = '--states' in sys.argv
    imgs, labs = [], []
    md = MD(COI)
    for idx, lab, desc in LEVELS:
        frames = start_level(md, idx)
        md.run(75)                                   # fade-in, Mickey drops in
        if save_states:
            open(os.path.join(STATES, 'coi_game_lvl%02d.state' % idx), 'wb').write(md.save())
        x0 = md.ram16(0xFFDC10)
        a = shot(md)
        for f in range(120):                         # walk right, jumping (C) now and then
            md.run(1, 'RIGHT+C' if f % 40 >= 28 else 'RIGHT')
        md.hold(0)
        md.run(2)
        x1 = md.ram16(0xFFDC10)
        b = shot(md)
        info = (idx, lab, md.ram16(0xFF44), md.ram16(0xFF46), md.ram8(0xF40E), md.zram[0x1C0C],
                md.ram16(0xF322), md.ram16(0xF324), md.ram16(0xF320), x0, x1, frames, desc)
        print('lvl %2d %s round=%d act=%d F40E=%02x z1C0C=%02x power=%d tries=%d items=%d '
              'mickeyX %d->%d  (%d frames from power-on)  %s' % info)
        imgs += [a, b]
        labs += ['%02d %s start' % (idx, lab), '%02d walked X %d->%d' % (idx, x0, x1)]
    grid(imgs, 8, labs, out)
    print('grid ->', out)


if __name__ == '__main__':
    main()
