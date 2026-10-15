"""Castle of Illusion sound interface (Z80 SMPS driver) - see docs/re_coi_game.md section 4.

    from coi_game_sound import play_sound, stop_sound, fade_out, set_pause, driver_state
    play_sound(md, 0x82)      # Enchanted Forest music
    play_sound(md, 0xC0)      # item pickup SFX
    stop_sound(md)            # command $D1

How the game does it (68000 routine $079F86, reached via JSR $79F86 or JSR $0476.w):
    the caller stores the ID in RAM $FFF40E, then the routine grabs the Z80 bus and writes
        ID <  $A0  (music)          -> Z80 RAM $1C0C  (direct "play music" slot)
        ID >= $A0  (SFX / commands) -> Z80 RAM $1C0A  (SFX queue slot 1, priority checked)
The Z80 driver also polls a second queue slot $1C0B that the game never uses: a port can put its
own SFX requests there so they never collide with a request the game made in the same frame.

Python writes here happen between emulated frames, so no bus request is needed (the Z80 is never
mid-instruction).  In a real-time core, do it the way the 68000 does: BUSREQ, write, release.

VERIFICATION NOTE: the tools DLL stubs the YM2612, whose status register never reports a timer
overflow, so the driver's main loop ($006F) never ticks and requests stay in the mailbox forever.
enable_fake_ym_timers() patches the *Z80 RAM copy* of the driver so that the two timer flags come
from a byte the script sets every frame ($1800 = fake YM status).  This is only an analysis aid for
the stubbed DLL; the port's real YM2612 has working timers and needs no patch.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coi_game_util import MD, COI  # noqa: E402

# ------------------------------------------------------------------ Z80 driver addresses
Z_BANK = 0x1C00        # word (LE), 68k bank base = $1C00<<16 | ($1C01&$80)<<8  -> $070000
Z_TABLES = 0x1C02      # word (LE), Z80 ptr to master table ($8000 -> ROM $070000)
Z_CUR = 0x1C09         # request being processed / $80 = idle (0 = re-init driver)
Z_SFXQ1 = 0x1C0A       # SFX/command queue slot 1 (game uses this one)
Z_SFXQ2 = 0x1C0B       # SFX/command queue slot 2 (unused by the game)
Z_MUSIC = 0x1C0C       # direct music request slot
Z_FADE = 0x1C0F        # fade-out counter (set to $28 by command $D0)
Z_PAUSE = 0x1C12       # pause state: 0 run, 1/2 paused, $80 = resume request
Z_PAUSEREQ = 0x1C13    # 68k sets 1 while the game is paused (checked by pause SFX $C1)
Z_LASTCMD = 0x1C1B     # last accepted queue request
Z_PRIO = 0x1C1C        # priority of the current SFX
TRACK_BASE = 0x1C40    # 20 tracks of $30 bytes: $1C40..$1FA0
TRACKS = ([('M0', 0x1C40)] + [('M%d' % i, 0x1C70 + 0x30 * (i - 1)) for i in range(1, 7)]
          + [('MPSG%d' % i, 0x1D90 + 0x30 * (i - 1)) for i in range(1, 4)]
          + [('SPC%d' % i, 0x1E20 + 0x30 * (i - 1)) for i in range(1, 3)]
          + [('SFX%d' % i, 0x1E80 + 0x30 * (i - 1)) for i in range(1, 7)])

# ------------------------------------------------------------------ sound IDs
MUSIC = {
    0x81: 'Title / opening story ("Once upon a mouse...")',
    0x82: 'Enchanted Forest (1-1, 1-3)',
    0x83: 'Spider-web cavern (1-4) / inside the tea cup (4-T-1..3)',
    0x84: 'Enchanted Forest act 2 (1-2, tree tops)',
    0x85: 'unused track (valid pointer, not in sound test, never requested)',
    0x86: 'unused/obj-driven track (sound test 05)',
    0x87: 'Toyland act 1 (2-1)',
    0x88: 'Toyland act 2/3 (2-2, 2-3)',
    0x89: 'Round clear / gem tally screen',
    0x8A: 'Mickey loses a try (death jingle)',
    0x8B: 'The Storm act 1 (3-1)',
    0x8C: 'The Storm ruins (3-2, 3-3)',
    0x8E: 'Library/kitchen shelves (4-1-1..4-1-5)',
    0x90: 'Ending: Mickey & Minnie reunited / staff credits (mode $16)',
    0x91: 'Dessert land (4-2)',
    0x92: 'never requested, not in sound test: header lives in Z80 RAM $1900 (block uploaded from ROM $79C20); the driver does start 9 tracks from it',
    0x93: 'special item jingle (object at $0832, sets $F443)',
    0x94: 'track (sound test 08)',
    0x96: 'Clock Tower act 1 (5-1, 5-S)',
    0x98: 'Clock Tower act 2 (5-2)',
    0x99: 'Boss defeated / gem appears',
    0x9A: 'Final battle - Mizrabel (L-B)',
    0x9B: 'Mizrabel second phase (final boss event $1088A)',
    0x9C: 'track (sound test 22)',
    0x9D: 'Game over / continue screen',
    0x9E: 'Castle: old man story + hallway ("What kind of room...")',
    0x9F: 'Boss battle (all round bosses)',
}
INVALID_MUSIC = {0x8D, 0x8F, 0x95, 0x97}   # null pointers in the music table -> crash/garbage
SFX_RANGE = range(0xA0, 0xC7)              # normal SFX (table at Z80 $0F00)
SPECIAL_SFX_RANGE = range(0xC8, 0xD0)      # "special" SFX (table at Z80 $0F4E); $C7 is invalid
COMMANDS = {0xD0: 'fade out music', 0xD1: 'stop all sound (driver reset)',
            0xD2: 'silence PSG', 0xD3: 'stop special-SFX tracks'}
# ids $D4-$D8 index past the command jump table (garbage); $D9-$FF behave like $D1 (reset)

# sound test number (1..25, RAM $FFF41F = n-1) -> driver ID (ROM table $009F1E)
SOUND_TEST = [0x81, 0x82, 0x83, 0x84, 0x86, 0x87, 0x93, 0x94, 0x88, 0x8B, 0x8C, 0x8E, 0x91, 0x96,
              0x98, 0x89, 0x8A, 0x90, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F]

# level index ($FFFF42) -> music started by level init (ROM table $0096B0)
LEVEL_MUSIC = [0x82, 0x84, 0x82, 0x83, 0x9F, 0x87, 0x88, 0x88, 0x9F, 0x8B, 0x8C, 0x8C, 0x9F, 0x8E,
               0x8E, 0x8E, 0x8E, 0x8E, 0x83, 0x83, 0x83, 0x91, 0x9F, 0x96, 0x96, 0x98, 0x9F, 0x9A]


# ------------------------------------------------------------------ requests
def play_sound(md, sid, slot2=False, mirror_ram=False):
    """Request music ($81-$9F) or SFX/command ($A0-$D3) exactly like 68k routine $79F86.

    slot2=True puts an SFX into the (game-unused) second queue slot $1C0B.
    mirror_ram=True also stores the ID in $FFF40E like the game's callers do (only the options
    screen reads it back)."""
    sid &= 0xFF
    if mirror_ram:
        md.set8(0xFFF40E, sid)
    z = md.zram
    if sid < 0xA0:
        z[Z_MUSIC] = sid
    else:
        z[Z_SFXQ2 if slot2 else Z_SFXQ1] = sid


def stop_sound(md):
    play_sound(md, 0xD1)


def fade_out(md):
    play_sound(md, 0xD0)


def set_pause(md, paused):
    """Mirror of the game's pause hook $9770 (called when START toggles $FFF41D)."""
    z = md.zram
    if paused:
        z[Z_SFXQ1] = 0xC1          # pause SFX; its track data sets $1C12=1 while $1C13!=0
        z[Z_PAUSEREQ] = 1
    else:
        z[Z_PAUSE] = 0x80          # resume request
        z[Z_PAUSEREQ] = 0


# ------------------------------------------------------------------ verification helpers
FAKE_STATUS = 0x1800   # free Z80 RAM ($1780-$18FF is never written by the driver)


def enable_fake_ym_timers(md):
    """Patch the Z80 RAM driver copy so timer flags come from $1800 (see module doc)."""
    z = md.zram
    assert bytes(z[0x0077:0x007A]) == b'\x3a\x00\x40', 'driver not loaded yet'
    stub_b = [0x21, 0x00, 0x18, 0xCB, 0x8E, 0xC3, 0xC7, 0x00]   # ld hl,$1800 / res 1,(hl) / jp $00C7
    stub_a = [0x21, 0x00, 0x18, 0xCB, 0x86, 0xC3, 0xAD, 0x00]   # ld hl,$1800 / res 0,(hl) / jp $00AD
    for i, v in enumerate(stub_b):
        z[0x1810 + i] = v
    for i, v in enumerate(stub_a):
        z[0x1820 + i] = v
    z[0x0077:0x007A] = (0x3A, 0x00, 0x18)   # ld a,($4000) -> ld a,($1800)
    z[0x0092:0x0095] = (0x3A, 0x00, 0x18)
    z[0x0082:0x0085] = (0xCD, 0x10, 0x18)   # call $00C7 -> call stub_b
    z[0x0099:0x009C] = (0xCD, 0x20, 0x18)   # call $00AD -> call stub_a
    z[FAKE_STATUS] = 0


def run_ticking(md, frames, buttons=None):
    """Run frames, raising both fake YM timer flags once per frame."""
    if buttons is not None:
        md.hold(buttons)
    for _ in range(frames):
        md.zram[FAKE_STATUS] |= 3
        md.run(1)


def driver_state(md):
    z = md.zram
    tr = {}
    for name, a in TRACKS:
        if z[a] & 0x80:
            tr[name] = dict(flags=z[a], chan=z[a + 1], ptr=z[a + 3] | (z[a + 4] << 8))
    return dict(cur=z[Z_CUR], q1=z[Z_SFXQ1], q2=z[Z_SFXQ2], music_req=z[Z_MUSIC],
                last=z[Z_LASTCMD], prio=z[Z_PRIO], fade=z[Z_FADE], pause=z[Z_PAUSE], tracks=tr)


def fmt_state(s):
    t = ' '.join('%s:%02x@%04x' % (k, v['flags'], v['ptr']) for k, v in s['tracks'].items())
    return 'cur=%02x q1=%02x mus=%02x last=%02x fade=%02x pause=%02x | %s' % (
        s['cur'], s['q1'], s['music_req'], s['last'], s['fade'], s['pause'], t or '(silent)')


def _active(md, names):
    z = md.zram
    return [n for n, a in TRACKS if n.startswith(names) and z[a] & 0x80]


def verify_all_ids(md_boot_state):
    """Request every ID $81..$D3 from a quiet, ticking driver and report what the driver does."""
    md = MD(COI)
    res = {}
    for sid in list(range(0x81, 0xA0)) + list(range(0xA0, 0xD4)):
        md.load(md_boot_state)
        play_sound(md, sid)
        ptr0 = None
        run_ticking(md, 3)
        z = md.zram
        act = [(n, z[a + 3] | (z[a + 4] << 8)) for n, a in TRACKS if z[a] & 0x80]
        run_ticking(md, 90)
        act2 = [(n, z[a + 3] | (z[a + 4] << 8)) for n, a in TRACKS if z[a] & 0x80]
        moved = len(set(act) - set(act2)) if act else 0
        res[sid] = (len(act), [n for n, _ in act], moved, z[Z_CUR], z[Z_PAUSE], md.z80()['pc'])
    return res


def verify_in_game():
    """Level start request, pause/unpause, SFX from the game, using the level 1-1 state."""
    here = os.path.dirname(os.path.abspath(__file__))
    st = os.path.join(os.path.dirname(here), 'states', 'coi_game_lvl00.state')
    md = MD(COI)
    md.load(open(st, 'rb').read())
    print('lvl 1-1 state: pending music slot $1C0C = %02X (request from level init $9686)'
          % md.zram[Z_MUSIC])
    enable_fake_ym_timers(md)
    run_ticking(md, 10)
    print('  after 10 ticking frames:', fmt_state(driver_state(md)))
    md.watch(0xFFF41D, 0xFFF41D)
    run_ticking(md, 2, 'START'); run_ticking(md, 20, 0)
    print('  START (pause) +20f : $F41D=%d $1C13=%d ->' % (md.ram8(0xFFF41D), md.zram[Z_PAUSEREQ]),
          fmt_state(driver_state(md)))
    run_ticking(md, 80)
    print('  paused +100f       :', fmt_state(driver_state(md)))
    run_ticking(md, 2, 'START'); run_ticking(md, 20, 0)
    print('  START (resume) +20f: $F41D=%d $1C13=%d ->' % (md.ram8(0xFFF41D), md.zram[Z_PAUSEREQ]),
          fmt_state(driver_state(md)))
    run_ticking(md, 30, 'C')
    run_ticking(md, 1, 0)
    print('  jump (C)      :', fmt_state(driver_state(md)))
    # play arbitrary ids in the running game
    play_sound(md, 0x9F)
    run_ticking(md, 5)
    print('  play_sound($9F) during gameplay:', fmt_state(driver_state(md)))
    play_sound(md, 0xC4, slot2=True)
    run_ticking(md, 2)
    print('  play_sound($C4, slot2) 1-up SFX:', fmt_state(driver_state(md)))
    stop_sound(md)
    run_ticking(md, 2)
    print('  stop_sound                     :', fmt_state(driver_state(md)))


def verify_sound_test():
    """Options SOUND TEST: every number 01..25 -> ID requested by the game ($9F1E table)."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    from coi_game_levelselect import to_options, press_until
    md = MD(COI)
    to_options(md)
    press_until(md, 'DOWN', lambda: md.ram8(0xFFF41E) == 1, 3, 'cursor on SOUND TEST')
    md.run(40)
    got = []
    for n in range(25):
        if n:
            cur = md.ram8(0xFFF41F)
            press_until(md, 'C', lambda: md.ram8(0xFFF41F) != cur, 3, 'next sound')
            md.run(3)
        got.append((md.ram8(0xFFF41F) + 1, md.ram8(0xFFF40E), md.zram[Z_MUSIC]))
    ok = all(g[1] == SOUND_TEST[i] and g[2] == SOUND_TEST[i] for i, g in enumerate(got))
    print('sound test numbers -> $F40E / Z80 $1C0C:',
          ' '.join('%02d:%02X' % (n, i) for n, i, _ in got), '| matches table:', ok)


if __name__ == '__main__':
    import collections
    md = MD(COI)
    md.run(120)                      # driver uploaded at boot ($79EE0 from $03BC)
    enable_fake_ym_timers(md)
    run_ticking(md, 5)
    print('boot      ', fmt_state(driver_state(md)))
    for sid in (0x82, 0x9F):
        play_sound(md, sid)
        run_ticking(md, 30)
        print('music %02x  ' % sid, fmt_state(driver_state(md)))
    play_sound(md, 0xC0)
    run_ticking(md, 2)
    print('sfx c0    ', fmt_state(driver_state(md)))
    fade_out(md)
    run_ticking(md, 10)
    print('fade (10f)', fmt_state(driver_state(md)))
    run_ticking(md, 200)
    print('fade done ', fmt_state(driver_state(md)))
    play_sound(md, 0x87)
    run_ticking(md, 10)
    print('music 87  ', fmt_state(driver_state(md)))
    stop_sound(md)
    run_ticking(md, 3)
    print('stop      ', fmt_state(driver_state(md)))
    quiet = md.save()
    print()
    res = verify_all_ids(quiet)
    for sid, (n, names, moved, cur, pause, zpc) in res.items():
        print('%02X  tracks=%d moved=%d cur=%02X z80pc=%04X  %s' % (sid, n, moved, cur, zpc, ','.join(names)))
    print()
    verify_in_game()
    print()
    verify_sound_test()
