"""World of Illusion cheats (see docs/re_woi_game.md section 4).

Two implementations for every cheat:
  * ROM patches (preferred for a port: one word each, no per-frame work, both players at once);
  * per-frame RAM writes (do them once per frame, e.g. after the VBlank handler / before the next
    main-loop iteration; all addresses are plain RAM so any moment between frames is safe).

    from woi_game_cheats import apply_patches, ram_cheats
    apply_patches(md, ['invincible', 'infinite_lives'])
    ... each frame: ram_cheats(md, health=True)

Player objects: P1 = $FFA000 (1P: the only player; 2P: Mickey if $FFFFD8 bit0 = 1),
P2 = $FFA080 (2P only).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

P1, P2 = 0xFFA000, 0xFFA080
HEARTS = 0x38          # byte, 0..8 (new game / respawn = 5, max 8)
INVUL_TIMER = 0x35     # byte, bits 0-6 frames left, bit 7 = do not blink
INVUL_FLAGS = 0x3E     # byte, bit 0 = invulnerable (checked by damage handler $F022)
HIT_FLAGS = 0x0F       # byte, bit 0 = hit this frame (set by enemy collision $F358)
HIT_DAMAGE = 0x37      # byte, extra damage (hearts lost = value + 1)
DEAD_FLAGS = 0x6D      # byte, bit0 dying, bit1 fell into a deadly tile, bit7 death anim finished
TRIES = 0xFFFD52       # word, shared by both players, 0..99
HUD_REFRESH = 0xFFFD54  # byte, set non-zero to redraw the TRIES counter
CARDS = 0xFFFCC2       # word, playing cards collected 0..51 (52nd card = extra try)
PMODE = 0xFFFFD8       # byte, bit 1 = 2-player game

# name -> list of (ROM address, original word, patched word)
PATCHES = {
    # damage handler $F000: "tst.b $3e(a0) / bne.b $f040" -> "bra.b $f040": enemy/hazard hits are
    # ignored exactly as while blinking after a hit.  Deadly tiles (pits, $C830) still kill.
    'invincible': [(0x00F026, 0x6618, 0x6018)],
    # "sub.b d7,$38(a0)" -> "tst.b $38(a0)" in both damage paths ($F098 normal hit, $F0D0 hit that
    # also starts the invulnerability timer): hits still knock the player back but cost no heart.
    'infinite_health': [(0x00F098, 0x9F28, 0x4A28), (0x00F0D0, 0x9F28, 0x4A28)],
    # "subq.w #1,$fd52.w" -> "tst.w $fd52.w" in the lose-a-try routine $F0EE (called by the heart
    # death $F0B2/$F0DA and by the deadly-tile death $C85C); keeps the following bpl valid.
    'infinite_lives': [(0x00F0EE, 0x5378, 0x4A78)],
}


def rom16(md, a):
    r = md.rom
    return (r[a] << 8) | r[a + 1]


def apply_patches(md, names):
    for n in names:
        for addr, orig, new in PATCHES[n]:
            cur = rom16(md, addr)
            if cur not in (orig, new):
                raise RuntimeError('%s: unexpected word %04X at %06X' % (n, cur, addr))
            md.patch16(addr, new)


def remove_patches(md, names):
    for n in names:
        for addr, orig, new in PATCHES[n]:
            md.patch16(addr, orig)


def players(md):
    return (P1, P2) if md.ram8(PMODE) & 2 else (P1,)


def ram_cheats(md, lives=False, tries=9, health=False, hearts=8, invincible=False, cards=None):
    """Per-frame RAM cheats.  invincible=True keeps the post-hit invulnerability running without
    blinking ($35 = $FF -> bit 7 no-blink, $3E bit 0 set by the countdown at $EFC4).  Side effect:
    $3E != 0 also disables the 2P player-on-player contact check at $EACE."""
    if lives and md.ram16(TRIES) < tries:
        md.set16(TRIES, tries)
        md.set8(HUD_REFRESH, 0xFF)
    for p in players(md):
        if health and md.ram8(p + HEARTS) != 0 and md.ram8(p + DEAD_FLAGS) == 0:
            md.set8(p + HEARTS, hearts)
        if invincible and md.ram8(p + DEAD_FLAGS) == 0:
            md.set8(p + INVUL_TIMER, 0xFF)
            md.set8(p + INVUL_FLAGS, md.ram8(p + INVUL_FLAGS) | 1)
    if cards is not None:
        md.set16(CARDS, cards)


def inject_hit(md, p=P1, damage=0):
    """Simulate an enemy touch exactly like the collision code $F358 does (hit flag bit 0);
    damage = extra hearts ($37: hearts lost = damage + 1; $7C = certain death as used at $8676)."""
    md.set8(p + HIT_DAMAGE, damage)
    md.set8(p + HIT_FLAGS, md.ram8(p + HIT_FLAGS) | 1)


# ------------------------------------------------------------------ verification
def _walk(md, frames, cheat=None, two=False):
    """Hold RIGHT (both pads in 2P) for `frames`, applying per-frame RAM cheats; returns the
    minimum hearts seen per player and whether a knock-back ($3F bit 1) happened."""
    md.hold('RIGHT')
    md.hold2('RIGHT' if two else 0)
    mins = [99, 99]
    knock = [False, False]
    for f in range(frames):
        if cheat:
            ram_cheats(md, **cheat)
        md.run(1)
        for k, p in enumerate((P1, P2)):
            if k == 1 and not two:
                continue
            mins[k] = min(mins[k], md.ram8(p + HEARTS))
            knock[k] |= bool(md.ram8(p + 0x3F) & 2)
    md.hold(0)
    md.hold2(0)
    return mins, knock


def _deaths(md, n=3, cheat=None, two=False):
    """Inflict n certain-death hits (alternating players in 2P) and let the game respawn."""
    log = []
    for i in range(n):
        p = P2 if (two and i % 2) else P1
        inject_hit(md, p, 0x7C)
        for f in range(400):
            if cheat:
                ram_cheats(md, **cheat)
            md.run(1)
        log.append((md.ram16(TRIES), md.ram16(0xFFFFD0) & 0x7F, md.ram8(P1 + HEARTS), md.ram8(P2 + HEARTS)))
    return log


def main():
    from woi_game_levelselect import start_area
    from woi_game_util import MD, WOI, shot, grid, SCRATCH
    md = MD(WOI)
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRATCH, 'woi_game')
    os.makedirs(out, exist_ok=True)
    imgs, labs = [], []
    for pm, two in (('1P_MICKEY', False), ('2P', True)):
        start_area(md, 1, 0, pm)
        md.run(60)
        base = md.save()
        cases = [('none', [], None), ('patch invincible', ['invincible'], None),
                 ('patch infinite_health', ['infinite_health'], None),
                 ('ram health=8', [], {'health': True}),
                 ('ram invincible', [], {'invincible': True})]
        for name, patches, cheat in cases:
            md.load(base)
            remove_patches(md, PATCHES.keys())
            apply_patches(md, patches)
            mins, knock = _walk(md, 1250, cheat, two)
            lab = '%s %s: min hearts %s knockback %s end %d/%d tries %d' % (
                pm, name, mins[:2 if two else 1], knock[:2 if two else 1], md.ram8(P1 + HEARTS),
                md.ram8(P2 + HEARTS), md.ram16(TRIES))
            print(lab)
            imgs.append(shot(md))
            labs.append(lab[:60])
        for name, patches, cheat in (('none', [], None), ('patch infinite_lives', ['infinite_lives'], None),
                                     ('ram lives', [], {'lives': True, 'tries': 3})):
            md.load(base)
            remove_patches(md, PATCHES.keys())
            apply_patches(md, patches)
            log = _deaths(md, 4, cheat, two)
            lab = '%s deaths %s: (tries, mode, h1, h2) %s' % (pm, name, log)
            print(lab)
            imgs.append(shot(md))
            labs.append(lab[:60])
    remove_patches(md, PATCHES.keys())
    grid(imgs, 4, labs, os.path.join(out, 'woi_game_cheats.png'))


if __name__ == '__main__':
    main()
