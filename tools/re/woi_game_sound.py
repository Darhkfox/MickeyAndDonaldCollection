"""World of Illusion sound interface (SMPS-type Z80 driver) - see docs/re_woi_game.md section 5.

    from woi_game_sound import play_sound, stop_sound, fade_out, driver_state
    play_sound(md, 0x84)      # Level 1 forest music
    play_sound(md, 0xA0)      # SFX $A0 (item pickup)
    stop_sound(md)            # command $EB (stop everything)

How the game does it (68000):
  * music / driver commands: `move.b #id,$FFFD2C`  (area loader $AAE2 also mirrors the id in $FFFD2B
    = "current music", used to avoid restarting the same tune);
  * SFX: `move.b #id,$FFFD2A ; trap #9`  -> trap handler $0F6078 copies it into the first free byte of
    the 3-slot queue $FFFD2D..$FFFD2F;
  * every VBlank ($0F609E, called from the $D74 handler) grabs the Z80 bus and copies
        $FFFD2C (if != 0)                      -> Z80 $1C0A   (and nothing else that frame)
        else each of $FFFD2D/$2E/$2F that is >= $80 -> Z80 $1C0A, $1C0B, $1C0C (packed)
    then clears $FFFD2C..$FFFD2F.
  * The Z80 driver (IM 1, runs from the Z80 VBlank interrupt at $0038 - it does NOT depend on YM
    timers) moves the highest-priority mailbox entry into $1C09 (priority table, master entry 0)
    and executes it on the same interrupt.

play_sound() writes the 68000 mailbox exactly like the game, so the request reaches the driver on
the next VBlank.  play_sound(..., direct=True) writes Z80 $1C0A directly (between frames the Z80 is
never mid-instruction in this DLL; a real-time core must hold BUSREQ like $0F609E does).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROMPATH = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                       'World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin')

# ---------------------------------------------------------------- 68000 side
M68_MUSIC = 0xFFFD2C      # byte: music / command request (cleared by VBlank)
M68_CURMUSIC = 0xFFFD2B   # byte: id of the music the area loader last started
M68_SFXREQ = 0xFFFD2A     # byte: SFX id for trap #9
M68_SFXQ = 0xFFFD2D       # 3 bytes: SFX queue
# ---------------------------------------------------------------- Z80 side
Z_BANKSEL = 0x1C3D        # music bank index (0 -> ROM $0E8000, 1 -> ROM $0F0000)
Z_CUR = 0x1C09            # request being processed ($80 = idle)
Z_Q = (0x1C0A, 0x1C0B, 0x1C0C)   # mailbox
Z_FADE = 0x1C0D           # fade-out counter ($28 after command $EA)
Z_PAUSE = 0x1C10          # 68k writes 1 = pause, $80 = resume (mode $0C handler $2146)
Z_PRIO = 0x1C18           # priority of the SFX currently playing
Z_DAC = 0x1C40            # music DAC track
Z_FM = [0x1C70 + 0x30 * i for i in range(6)]     # music FM1..FM6
Z_PSG = [0x1D90 + 0x30 * i for i in range(3)]    # music PSG1..PSG3
Z_SFX = [0x1E20 + 0x30 * i for i in range(7)]    # SFX tracks
Z_SPECIAL = 0x1F70        # special-SFX track(s)

# music id -> bank (Z80 table $08E1); $64 = invalid id
MUSIC_BANK = [0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0x64, 0x64, 0x64,
              0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0]
BANK_BASE = {0: 0x0E8000, 1: 0x0F0000}

MUSIC = {
    0x81: 'unused tune (valid data, never requested, not in the sound test)',
    0x82: 'Attract: cast intro + title screen (sound test 1)',
    0x83: 'Story book (opening story, mode 6)',
    0x84: 'Level 1 forest (areas 0,1,2; demo)',
    0x85: 'Level 1 treehouse (2P area 3) / spider cave (area 4); L5 2P door stage 1',
    0x86: 'Level 2 sky / rocky mountain (areas 0, 16); L5 2P door stage 2',
    0x87: 'Level 2 Mickey route - storm mountain (areas 12-14)',
    0x88: 'Level 2 Donald route - underground river raft (areas 1-11)',
    0x89: 'Level 2 2P route - cave (area 15)',
    0x8A: 'Level 3 under the sea (most areas); L5 2P door stage 3',
    0x8B: 'Level 3 Donald route - tropical island surface (area 3)',
    0x8C: 'Level 3 sunken ship (area 7)',
    0x8D: 'Level 4 giant house (hub rooms, post-boss kitchen)',
    0x8E: 'Level 4 jewellery/toy box (area 12); L5 2P door stage 4',
    0x8F: 'Level 4 candy land (areas 11, 14, 15)',
    0x90: 'Level 4 flying over the sky (area 10)',
    0x94: 'Level 5 magic garden (start, Donald route, areas 26-29)',
    0x95: 'Level 5 2P route - door hall (area 13) / constellation stage entries (18-22)',
    0x96: 'Level 5 Mickey route - palace (areas 11, 12)',
    0x97: 'Boss battle (levels 1-4)',
    0x98: 'Final boss (L5 area 25)',
    0x99: 'Options card board / treasure chest after a boss',
    0x9A: 'Game over screen',
    0x9B: 'Ending part (object at $231F2)',
    0x9C: 'Ending part (object at $22656)',
    0x9D: 'Level 5 starry stage before the final boss (areas 23, 24)',
    0x9E: 'Ending part (object at $23208)',
    0x9F: 'Ending / staff roll (mode $11)',
}
INVALID_MUSIC = (0x91, 0x92, 0x93)        # bank table value $64 -> garbage bank
SFX_RANGE = range(0xA0, 0xE0)              # normal SFX (pointer table Z80 $0E00)
COMMANDS = {0xEA: 'fade out music', 0xEB: 'stop all sound (driver reset $0716)',
            0xEC: 'silence PSG', 0xED: 'stop special-SFX track(s) ($1F70)'}
# ids $EF-$FF also run the stop routine $0716; $E0-$E9 index the special-SFX table (only $E8/$E9
# fall inside it), $EE indexes past the command table.

# sound test (SOUND card): MUSIC 1..27 -> table $021F3E (odd bytes), SE 1..6 -> table $021F74
SOUND_TEST_MUSIC = [0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
                    0x8F, 0x90, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F]
SOUND_TEST_SE = [0xD3, 0xD4, 0xD5, 0xD6, 0xDE, 0xDF]


def _rom():
    return open(ROMPATH, 'rb').read()


def music_header(mid):
    """(bank, ROM address of the SMPS header, first-track Z80 pointer) for a music id."""
    r = _rom()
    b = MUSIC_BANK[mid - 0x81]
    if b not in BANK_BASE:
        return None
    base = BANK_BASE[b]
    w = lambda a: r[a] | (r[a + 1] << 8)  # noqa: E731
    table = base + (w(base + 4) - 0x8000)
    hdr = w(table + 2 * (mid - 0x81))
    h = base + (hdr - 0x8000)
    return b, h, w(h + 6)


def play_sound(md, sid, direct=False):
    """Request music ($81-$9F), SFX ($A0-$DF) or a command ($EA-$ED) like the game does."""
    sid &= 0xFF
    if direct:
        md.zram[Z_Q[0]] = sid
    elif sid < 0xA0 or sid >= 0xE0:
        md.set8(M68_MUSIC, sid)
    else:
        for a in range(M68_SFXQ, M68_SFXQ + 3):   # same as trap #9 ($0F6078)
            if md.ram8(a) == 0:
                md.set8(a, sid)
                break


def stop_sound(md):
    play_sound(md, 0xEB)
    md.set8(M68_CURMUSIC, 0)     # like $B3BC: the area loader will restart its music


def fade_out(md):
    play_sound(md, 0xEA)


def track_active(md, t):
    return bool(md.zram[t] & 0x80)


def driver_state(md):
    z = md.zram
    return {
        'cur': z[Z_CUR], 'bank': z[Z_BANKSEL], 'fade': z[Z_FADE], 'prio': z[Z_PRIO],
        'dac_ptr': z[Z_DAC + 3] | (z[Z_DAC + 4] << 8),
        'music_tracks': sum(track_active(md, t) for t in [Z_DAC] + Z_FM + Z_PSG),
        'sfx_tracks': sum(track_active(md, t) for t in Z_SFX),
        'special': track_active(md, Z_SPECIAL),
    }


def main():
    """Verify: every id through the game's mailbox from a running game, plus the SOUND card."""
    from woi_game_levelselect import start_area
    from woi_game_util import MD, WOI, to_board
    md = MD(WOI)
    start_area(md, 1, 0, '1P_MICKEY')
    md.run(30)
    base = md.save()
    print('id   result')
    for sid in list(range(0x81, 0xA0)) + list(range(0xA0, 0xE0)) + list(range(0xE0, 0x100)):
        md.load(base)
        play_sound(md, 0xEB)
        md.run(2)
        play_sound(md, sid)
        md.run(3)
        st = driver_state(md)
        if sid < 0xA0:
            hdr = music_header(sid)
            ok = hdr is not None and st['dac_ptr'] == hdr[2] and st['bank'] == hdr[0]
            txt = 'music bank %s tracks %d dac_ptr %04X expected %s -> %s   %s' % (
                st['bank'], st['music_tracks'], st['dac_ptr'],
                '%04X' % hdr[2] if hdr else 'INVALID', 'OK' if ok else 'BAD', MUSIC.get(sid, ''))
        elif sid < 0xE0:
            txt = 'sfx tracks %d special %d prio %02X' % (st['sfx_tracks'], st['special'], st['prio'])
        else:
            txt = 'cmd -> music tracks %d sfx %d special %d fade %02X  %s' % (
                st['music_tracks'], st['sfx_tracks'], st['special'], st['fade'], COMMANDS.get(sid, ''))
        print('%02X   %s' % (sid, txt))
    # stop / fade from a playing state
    md.load(base)
    play_sound(md, 0x84)
    md.run(3)
    fade_out(md)
    md.run(2)
    f1 = driver_state(md)
    md.run(300)
    print('fade: right after %s, 300 frames later %s' % (f1, driver_state(md)))
    md.load(base)
    stop_sound(md)
    for f in range(4):
        md.run(1)
        print('stop +%d: %s' % (f + 1, driver_state(md)))
    # sound test card: MUSIC n / SE n -> id actually sent to the driver
    to_board(md)
    md.press('DOWN', 3, 20)
    md.press('C', 3, 40)                     # open the SOUND card (shows MUSIC 1)
    got = []
    md.watch(M68_SFXREQ, M68_MUSIC)
    for n in range(27 + 6):
        md.watch_hits()                       # drain (the C press plays card SFX $C3)
        md.press('A', 3, 6)
        hits = [h for h in md.watch_hits() if h['value'] & 0xFF]
        got.append(hits[-1]['value'] & 0xFF if hits else None)
        md.press('C', 3, 40)                  # next entry (wait for the card flip)
    md.unwatch()
    exp = SOUND_TEST_MUSIC + SOUND_TEST_SE
    print('sound test ids:', ' '.join('%02X' % g if g else '--' for g in got))
    print('sound test table match:', got == exp)


if __name__ == '__main__':
    main()
