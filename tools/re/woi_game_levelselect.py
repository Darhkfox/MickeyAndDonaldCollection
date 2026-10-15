"""World of Illusion level select (see docs/re_woi_game.md section 3).

    from woi_game_levelselect import start_level, STAGES, MODES
    md = MD(WOI)
    start_level(md, 7, '2P')        # power-on -> stage index 7 in 2-player mode, returns when playable

Technique (all from a clean power-on, deterministic):
  1. tap START through SEGA / Illusion Shot / cast / title / story book until the options card board
     (main mode $FFFFD0 == $800D) and wait until the board is interactive (sub-state $FFA850 == 1);
  2. RIGHT, C  -> PASSWORD card (board sub-state $FFA850 == 2);
  3. write the 4 password cards $FFA854..$FFA857 with the game's own password for
     (player mode, level) = table $0209A0[mode_index*5 + level-1]  (the card graphics are not
     redrawn, the validator only looks at RAM);
  4. press START: the game's validator $021DF6 sets $FFFFD8 (player mode), $FFFFDA (level),
     $FFFFDE (area 0, or 1 for 1P Donald level 4) and $FFFFE1 (progress byte), then fades out;
  5. for a non-first area: as soon as $FFFFDA holds the level (same frame as the START press is
     processed, the board is still fading for ~30 frames), overwrite $FFFFDE with the area index;
  6. mode 1 ($1F30) then sets tries/hearts exactly as for a new game and mode 2 loads the area
     (music from the area header, palette, objects); wait for mode 3 (gameplay).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from woi_game_util import MD, WOI, to_board, shot, grid, SCRATCH, mode  # noqa: E402

MODE = 0xFFFFD0
PMODE = 0xFFFFD8
LEVEL = 0xFFFFDA
AREA = 0xFFFFDE
PW = 0xFFA854

# player modes: value of $FFFFD8 and index into the password table (validator table $21E7A)
MODES = {
    '1P_MICKEY': (0x01, 0),
    '1P_DONALD': (0x00, 1),
    '2P': (0x03, 2),          # 2 players, pad 1 = Mickey, pad 2 = Donald
    '2P_SWAP': (0x02, 3),     # 2 players, pad 1 = Donald, pad 2 = Mickey
}
MODE_ALIASES = {'M': '1P_MICKEY', 'MICKEY': '1P_MICKEY', 'D': '1P_DONALD', 'DONALD': '1P_DONALD',
                '2': '2P', '2P_MICKEY': '2P', '2P_DONALD': '2P_SWAP'}

# password table $0209A0: 20 entries, index = mode_index*5 + (level-1)
PASSWORDS = [
    (1, 2, 0, 5), (3, 0, 2, 4), (2, 5, 4, 1), (0, 4, 5, 3), (4, 3, 2, 5),
    (2, 1, 0, 3), (5, 4, 3, 1), (3, 2, 0, 5), (0, 4, 2, 5), (5, 2, 3, 4),
    (2, 4, 5, 3), (5, 2, 1, 3), (4, 1, 2, 0), (1, 0, 3, 5), (3, 4, 5, 2),
    (4, 3, 1, 2), (1, 2, 5, 4), (5, 0, 1, 4), (3, 1, 4, 0), (5, 3, 4, 0),
]
CARD_NAMES = ['Daisy', 'Minnie', 'Goofy', 'Pete', 'Scrooge', 'Pluto']

# Stage list = the developers' (level, area) table at $021E8A (37 entries, unreferenced in the
# retail ROM), with the player modes whose normal route visits the area ('M' 1P Mickey,
# 'D' 1P Donald, '2' 2P) - see docs/re_woi_game.md section 3 for the route analysis.
STAGES = [
    # (level, area, modes, name)          modes: M = 1P Mickey, D = 1P Donald, 2 = 2 players
    (1, 0x00, 'MD2', 'L1 Enchanted forest - start'),
    (1, 0x01, 'D', 'L1 beanstalk (Donald route)'),
    (1, 0x02, 'MD', 'L1 sunny woods (Mickey route; Donald passes its start)'),
    (1, 0x03, '2', 'L1 treehouse scaffolding (2P route)'),
    (1, 0x04, 'MD2', 'L1 spider cave'),
    (1, 0x05, 'MD2', 'L1 boss - spider-web cave'),
    (2, 0x00, 'MD2', 'L2 rocky mountain start (magic carpet)'),
    (2, 0x01, 'D', 'L2 underground river raft (Donald route, areas 1-11)'),
    (2, 0x0C, 'M', 'L2 storm mountain (Mickey route, areas 12-14)'),
    (2, 0x0F, '2', 'L2 cave (2P route)'),
    (2, 0x10, 'MD2', 'L2 above the clouds'),
    (2, 0x11, 'MD2', 'L2 boss - stone door cave'),
    (3, 0x00, 'MD2', 'L3 under the sea - start'),
    (3, 0x01, 'MD2', 'L3 coral temple'),
    (3, 0x03, 'D', 'L3 tropical island (Donald route)'),
    (3, 0x04, 'M', 'L3 pink coral reef (Mickey route)'),
    (3, 0x05, '2', 'L3 ice-block ruins (2P route)'),
    (3, 0x06, 'MD2', 'L3 open sea 2'),
    (3, 0x07, 'MD2', 'L3 sunken ship'),
    (3, 0x08, 'MD2', 'L3 boss - inside the ship'),
    (3, 0x09, 'MD2', 'L3 after the boss (swim up = next level)'),
    (3, 0x0A, 'MD2', 'L3 open sea 1b'),
    (4, 0x00, 'M2', 'L4 giant house hub - start (Mickey / 2P)'),
    (4, 0x08, 'D', 'L4 storybook forest (Donald route)'),
    (4, 0x0A, 'MD2', 'L4 flying over the sky'),
    (4, 0x0C, '2', 'L4 toy / jewellery box (2P route)'),
    (4, 0x0F, 'M', 'L4 candy land (Mickey entry)'),
    (4, 0x10, 'MD2', 'L4 boss - candy land witch'),
    (4, 0x11, 'MD2', 'L4 after the boss - kitchen'),
    (5, 0x00, 'MD2', 'L5 magic garden - start'),
    (5, 0x01, 'D', 'L5 hedge garden (Donald route)'),
    (5, 0x0B, 'M', 'L5 palace hall (Mickey route)'),
    (5, 0x0D, '2', 'L5 door hall (2P route)'),
    (5, 0x17, 'MD2', 'L5 starry stage 1'),
    (5, 0x18, 'MD2', 'L5 starry stage 2'),
    (5, 0x19, 'MD2', 'L5 final boss'),
    (5, 0x1A, 'MD2', 'L5 falling through the forest (garden side area)'),
]


def _mode_key(m):
    m = str(m).upper()
    return MODE_ALIASES.get(m, m)


def password_for(level, pmode):
    """The game's own 4-card password for (level 1..5, player mode)."""
    return PASSWORDS[MODES[_mode_key(pmode)][1] * 5 + (level - 1)]


def start_area(md, level, area, pmode='1P_MICKEY', power_on=True, max_frames=1500):
    """Start (level, area) in the given player mode exactly like a password start.
    Returns the frame count at which gameplay (mode 3) began, or None."""
    key = _mode_key(pmode)
    if power_on:
        to_board(md)
    md.press('RIGHT', 3, 20)
    md.press('C', 3, 40)                       # open the PASSWORD card
    if md.ram16(0xFFA850) != 2:
        raise RuntimeError('password card not open (board sub-state %d)' % md.ram16(0xFFA850))
    for i, v in enumerate(password_for(level, key)):
        md.set8(PW + i, v)
    md.hold('START')
    for _ in range(60):                         # validator runs on the START edge
        md.run(1)
        if md.ram16(LEVEL) == level and (md.ram8(PMODE) & 3) == MODES[key][0]:
            break
    md.hold(0)
    else_ok = md.ram16(LEVEL) == level
    if not else_ok:
        raise RuntimeError('password not accepted')
    md.set16(AREA, area)                        # board is still fading out: safe window
    for f in range(max_frames):
        md.run(1)
        if md.ram16(MODE) & 0x7F == 3:
            return md.frame
    return None


PROGRESS = [0x00, 0x06, 0x0C, 0x16, 0x1D]     # $FFFFE1 per level as set by the password ($21E7E)


def start_area_direct(md, level, area, pmode='1P_MICKEY', power_on=True, max_frames=1500):
    """Alternative without the password card: START on the board, then at the first frame where
    the main mode $FFFFD0 reads 1 (new-game init $1F30 has not run yet) write $FFFFD8/$FFFFDA/
    $FFFFDE/$FFFFE0.  Produces the same RAM as start_area() (verified by RAM diff)."""
    key = _mode_key(pmode)
    if power_on:
        to_board(md)
    md.hold('START')
    for _ in range(200):
        md.run(1)
        if md.ram16(MODE) == 1:
            break
        if md.ram16(MODE) & 0x7F != 0x0D:
            raise RuntimeError('missed mode 1')
        md.hold(0)
    md.hold(0)
    md.set8(PMODE, (md.ram8(PMODE) & 0xFC) | MODES[key][0])
    md.set16(LEVEL, level)
    md.set16(AREA, area)
    md.set16(0xFFFFE0, PROGRESS[level - 1])
    for f in range(max_frames):
        md.run(1)
        if md.ram16(MODE) & 0x7F == 3:
            return md.frame
    return None


def start_level(md, index, mode='1P_MICKEY', power_on=True):
    """Start STAGES[index] in `mode` ('1P_MICKEY'/'M', '1P_DONALD'/'D', '2P', '2P_SWAP')."""
    level, area, modes, name = STAGES[index]
    return start_area(md, level, area, mode, power_on)


def demo_move(md, frames=60, two_players=False):
    """Walk right a little (both players in 2P) and jump once to prove the stage is playable."""
    md.hold('RIGHT')
    if two_players:
        md.hold2('RIGHT')
    md.run(frames - 12)
    md.hold('RIGHT+C')
    if two_players:
        md.hold2('RIGHT+C')
    md.run(12)
    md.hold(0)
    md.hold2(0)
    md.run(20)


def main():
    import time
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRATCH, 'woi_game')
    os.makedirs(out, exist_ok=True)
    md = MD(WOI)
    for mkey, letter in (('1P_MICKEY', 'M'), ('1P_DONALD', 'D'), ('2P', '2'), ('2P_SWAP', '2')):
        imgs, labs = [], []
        t0 = time.time()
        for i, (level, area, modes, name) in enumerate(STAGES):
            if letter not in modes:
                continue
            f = start_level(md, i, mkey)
            ok = f is not None
            pos = lambda p: (md.ram16(p + 0x10), md.ram16(p + 0x14))  # noqa: E731  (x, y)
            p0, p1 = pos(0xFFA000), pos(0xFFA080)
            moved1 = moved2 = False
            md.hold('RIGHT')                        # walk right, then jump: watch x and y
            md.hold2('RIGHT' if letter == '2' else 0)
            for f in range(60):
                if f == 48:
                    md.hold('RIGHT+C')
                    md.hold2('RIGHT+C' if letter == '2' else 0)
                md.run(1)
                moved1 |= pos(0xFFA000) != p0
                moved2 |= pos(0xFFA080) != p1
            md.hold(0)
            md.hold2(0)
            md.run(20)
            moved = moved1 and (moved2 or letter != '2')
            lab = '%02d L%d A%02X %s %s%s h=%d/%d t=%d m=%X' % (
                i, level, area, mkey, 'OK' if ok else 'FAIL', ' moved' if moved else ' STILL',
                md.ram8(0xFFA038), md.ram8(0xFFA0B8), md.ram16(0xFFFD52), md.ram16(MODE) & 0x7F)
            print(lab)
            imgs.append(shot(md))
            labs.append(lab)
        grid(imgs, 4, labs, os.path.join(out, 'woi_game_levelselect_%s.png' % mkey))
        print('%s: %d stages in %.0fs' % (mkey, len(imgs), time.time() - t0))


if __name__ == '__main__':
    main()
