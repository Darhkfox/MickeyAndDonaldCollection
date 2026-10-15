"""QuackShot (REV00) cheats: RAM addresses, per-frame RAM writes and ROM patches, with self-tests.

    from qs_game_cheats import PATCHES, apply_patch, per_frame
    apply_patch(md, 'invincible')            # ROM patch (loaded copy), any time
    per_frame(md, ['inf_lives', 'max_health'])   # call once per frame (after md.run(1))

    python qs_game_cheats.py                 # runs every verification test (prints PASS/FAIL)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qs_game_common import new_md, mode, wait_mode, run_until  # noqa: E402

# ---- RAM (all words unless noted) -------------------------------------------------------------
HEALTH = 0xFF8F96        # "Power": 0..8 hearts (new life = 5, max 8)
LIVES = 0xFF8F98         # "Donald" counter, 0..99 (binary)
TEMPER = 0xFF8F9A        # temper meter: 0..5 chilli peppers (5 -> quack attack)
POPCORN = 0xFF8F90       # popcorn ammo 0..95 ($5F); each shot costs 5, each pickup gives 5
GUM = 0xFF8F92           # bubble-gum ammo 0..50 ($32); each shot costs 1, pickup +5, Gyro refills to 50
WEAPON = 0xFF8F66        # 0 plunger, 1 popcorn, 2 bubble gum
PLUNGER = 0xFFEE48       # byte: plunger type 0 yellow / 1 red (climb) / 2 green (catch birds)
QUACK = 0xFFEE62         # byte: quack-attack (berserk, invincible) active
QUACK_TIMER = 0xFF8FA8   # word: quack-attack frames left (set to 360 = $168)
INVUL = 0xFFEE08         # byte: post-hit invulnerability active
INVUL_TIMER = 0xFFEE0A   # byte: post-hit invulnerability frames left (120 = $78 after a hit)
HIT = 0xFFEE09           # byte: "hit this frame" request (set by enemy collision)
EXTRA_DMG = 0xFF8FAA     # word: extra damage for the pending hit (bosses)
SCORE = 0xFFEF34         # long: score/10 (binary); HUD prints SCORE*10 as 8 digits
SCORE_PENDING = 0xFF8F7A  # word: points (/10) waiting to be counted into SCORE, 10 per frame
FREEMOVE = 0xFFEE07      # byte: unused debug free-movement mode (D-pad moves Donald 4 px/frame)

# ---- ROM patches: (addr, original words, new words, explanation) ---------------------------------
NOP = 0x4E71
PATCHES = {
    'inf_lives': [(0x00481E, [0x5378, 0x8F98], [0x4A78, 0x8F98],
                   'mode 5 death handler: subq.w #1,$8F98 -> tst.w $8F98 (keeps the following beq game-over test valid)')],
    'inf_health': [(0x005E16, [0x9F78, 0x8F96, 0x5378, 0x8F96], [NOP, NOP, NOP, NOP],
                    'damage apply $5E00: sub.w d7,$8F96 / subq.w #1,$8F96 -> nops (still knocked back)')],
    'invincible': [(0x00742C, [0x50F8, 0xEE09], [0x4A38, 0xEE09], 'st.b $EE09 -> tst.b $EE09 (terrain/enemy hit)'),
                   (0x00B866, [0x50F8, 0xEE09], [0x4A38, 0xEE09], 'st.b $EE09 -> tst.b $EE09 (object collision)'),
                   (0x00F414, [0x50F8, 0xEE09], [0x4A38, 0xEE09], 'st.b $EE09 -> tst.b $EE09 (projectiles)')],
    'inf_popcorn': [(0x005108, [0x5B78, 0x8F90], [NOP, NOP], 'popcorn shot: subq.w #5,$8F90 -> nops')],
    'inf_gum': [(0x005130, [0x5378, 0x8F92], [NOP, NOP], 'gum shot: subq.w #1,$8F92 -> nops')],
    'endless_quack': [(0x005FDE, [0x5378, 0x8FA8], [NOP, NOP], 'quack-attack timer: subq.w #1,$8FA8 -> nops')],
}


def rom16(md, a):
    r = md.rom
    return (r[a] << 8) | r[a + 1]


def apply_patch(md, name, undo=False):
    for addr, orig, new, _ in PATCHES[name]:
        for i, (o, n) in enumerate(zip(orig, new)):
            cur = rom16(md, addr + 2 * i)
            want_from, want_to = (n, o) if undo else (o, n)
            assert cur in (want_from, want_to), 'unexpected ROM word at $%06X: %04X' % (addr + 2 * i, cur)
            md.patch16(addr + 2 * i, want_to)


# ---- per-frame RAM cheats (write after each frame, i.e. before the next frame's game logic) ------
def per_frame(md, names):
    if mode(md) not in (0x05, 0x08):          # only while a level is running (gameplay / pause)
        return
    if 'inf_lives' in names:
        md.set16(LIVES, 9)
    if 'max_health' in names:
        md.set16(HEALTH, 8)
    if 'inf_ammo' in names:
        md.set16(POPCORN, 95); md.set16(GUM, 50)
    if 'max_temper' in names:                 # meter full; next chilli pepper starts quack attack
        md.set16(TEMPER, 4)
    if 'quack_attack' in names:               # permanent quack attack (invincible berserk Donald)
        md.set8(QUACK, 0xFF); md.set16(QUACK_TIMER, 360); md.set16(TEMPER, 5)
    if 'all_weapons' in names:                # bubble gum usable + green plunger (all abilities)
        md.set8(0xFFEEE7, 0xFF); md.set8(0xFFEEE4, 0xFF); md.set8(0xFFEEED, 0xFF)
        if md.ram8(PLUNGER) != 2:
            md.set8(PLUNGER, 2); md.set16(0xFFF594, 0x02A2); md.set8(0xFFEE02, 1)


# =====================================================================================================
# verification
S = os.environ.get('QS_OUT', os.path.join(HERE, 'qs_game_out'))


def _level(idx=7):
    from qs_game_levelselect import start_level
    md = new_md()
    assert start_level(md, idx)
    return md


def _walk_into_enemies(md, frames=200, cheats=()):
    """Transylvania 3-1: holding RIGHT runs into two enemies (control run: health 5 -> 3)."""
    log = {'hits': 0, 'health_min': 99, 'knock': 0}
    md.hold('RIGHT')
    for _ in range(frames):
        md.run(1)
        per_frame(md, cheats)
        if md.ram8(HIT):
            log['hits'] += 1
        if md.ram16(0xFFA2C2) & 0x7FFF == 0x1A:     # player state $1A = hurt/knock-back
            log['knock'] += 1
        log['health_min'] = min(log['health_min'], md.ram16(HEALTH))
    md.hold(0)
    return log


def test_control():
    md = _level()
    r = _walk_into_enemies(md)
    return r['hits'] >= 1 and r['health_min'] < 5, r


def test_invincible():
    md = _level()
    apply_patch(md, 'invincible')
    r = _walk_into_enemies(md)
    return r['hits'] == 0 and r['health_min'] == 5 and r['knock'] == 0, r


def test_inf_health_patch():
    md = _level()
    apply_patch(md, 'inf_health')
    r = _walk_into_enemies(md)
    return r['hits'] >= 1 and r['health_min'] == 5, r


def test_max_health_ram():
    md = _level()
    r = _walk_into_enemies(md, cheats=['max_health'])
    return r['hits'] >= 1 and r['health_min'] == 8, r


def _die(md):
    md.set8(0xFFEE58, 0xFF)                 # the flag set by every death path ($6A94/$6BF6/...)
    run_until(md, lambda d: mode(d) == 0x04, 60)
    wait_mode(md, 0x05, 900)
    md.run(30)


def test_inf_lives_patch():
    md = _level(); a = md.ram16(LIVES); _die(md); b = md.ram16(LIVES)
    md2 = _level(); apply_patch(md2, 'inf_lives'); _die(md2); c = md2.ram16(LIVES)
    return b == a - 1 and c == a and mode(md2) == 5, {'before': a, 'after_normal': b, 'after_patched': c,
                                                      'mode_patched': mode(md2)}


def test_game_over_avoided():
    md = _level(); apply_patch(md, 'inf_lives'); md.set16(LIVES, 1); _die(md)
    return mode(md) == 0x05 and md.ram16(LIVES) == 1, {'mode': mode(md), 'lives': md.ram16(LIVES)}


def _fire(md, weapon, frames=90):
    md.set16(WEAPON, weapon); md.set8(0xFFEE5B, 1)
    md.set16(POPCORN, 20); md.set16(GUM, 20); md.set8(0xFFEEE7, 0xFF)
    for _ in range(3):
        md.press('B', 2, 28)                # default control: B = SHOOT
    return md.ram16(POPCORN), md.ram16(GUM)


def test_ammo():
    md = _level(); p, g = _fire(md, 1); md.run(30); p2, g2 = _fire(md, 2)
    normal = (p, g2)
    md = _level(); apply_patch(md, 'inf_popcorn'); apply_patch(md, 'inf_gum')
    p, g = _fire(md, 1); md.run(30); p2, g2 = _fire(md, 2)
    patched = (p, g2)
    return normal[0] < 20 and normal[1] < 20 and patched == (20, 20), {'normal(popcorn,gum)': normal,
                                                                        'patched': patched}


def test_quack_attack():
    md = _level()
    r = _walk_into_enemies(md, cheats=['quack_attack'])
    md.png(os.path.join(S, 'qs_game_cheat_quack.png'), 1)
    return r['hits'] == 0 and r['health_min'] == 5, r


def test_quack_expiry():
    """Start quack attack the way the 5th chilli does ($7D08-$7D2C), check it ends after $168 frames
    ($5FD8: temper reset to 0, $EE08/$EE0A = 120-frame blink), and that 'endless_quack' keeps it on."""
    md = _level()
    md.set16(TEMPER, 5); md.set8(QUACK, 0xFF); md.set16(QUACK_TIMER, 0x168)
    md.run(0x168 + 5)
    normal = (md.ram8(QUACK), md.ram16(TEMPER), md.ram8(INVUL))
    md = _level(); apply_patch(md, 'endless_quack')
    md.set16(TEMPER, 5); md.set8(QUACK, 0xFF); md.set16(QUACK_TIMER, 0x168)
    md.run(0x168 * 2)
    patched = md.ram8(QUACK)
    return normal == (0, 0, 0xFF) and patched == 0xFF, {'normal(quack,temper,invul)': normal,
                                                         'patched_quack_after_720f': patched}


def test_freemove():
    md = _level()
    md.set8(FREEMOVE, 0xFF)
    y0 = md.ram16(0xFFA2D4)
    md.hold('UP'); md.run(30); md.hold(0); md.run(5)
    y1 = md.ram16(0xFFA2D4)
    md.run(60)
    y2 = md.ram16(0xFFA2D4)                 # no gravity in free-move
    md.png(os.path.join(S, 'qs_game_cheat_freemove.png'), 1)
    return y1 <= y0 - 100 and y2 == y1, {'y0': y0, 'y_after_up': y1, 'y_60f_later': y2}


def test_all_weapons():
    md = _level(1)                            # Duckburg 1-2, new-game state (yellow plunger, no gum)
    per_frame(md, ['all_weapons'])
    md.run(2)
    md.press('START', 4, 60)                  # pause menu shows BUBBLE GUM now
    md.png(os.path.join(S, 'qs_game_cheat_weapons.png'), 1)
    return mode(md) == 0x08 and md.ram8(PLUNGER) == 2, {'mode': mode(md), 'plunger': md.ram8(PLUNGER)}


if __name__ == '__main__':
    os.makedirs(S, exist_ok=True)
    tests = [test_control, test_invincible, test_inf_health_patch, test_max_health_ram, test_inf_lives_patch,
             test_game_over_avoided, test_ammo, test_quack_attack, test_quack_expiry, test_freemove,
             test_all_weapons]
    for t in tests:
        ok, info = t()
        print('%-24s %s  %s' % (t.__name__, 'PASS' if ok else 'FAIL', info), flush=True)
