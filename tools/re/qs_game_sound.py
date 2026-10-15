"""QuackShot (REV00) sound: IDs, how to request them, and play_sound()/stop_sound() for a running game.

Driver: Sega "SMPS Z80" (Sonic-2 style), Z80 program ROM $784E0 ($DC6 bytes) -> Z80 $0000, SFX bank
ROM $792A6 ($E1E bytes) -> Z80 $0DD0 (normal) or ending bank ROM $7A0C4 ($6D8 bytes) -> Z80 $0F00.
Music data is read from the 68k ROM through the Z80 bank window: bank $0E -> ROM $70000-$77FFF.

Command interface (Z80 RAM):
    $1C0A (,$1C0B,$1C0C)  request queue: write an ID with bit 7 set; the driver copies it to $1C09 if its
                          priority (table ROM $7009C) >= current priority $1C18, then clears the slot.
    $1C09                 ID being started (reset to $80 after processing)
    $1C07                 $80 = timer-driven tick (YM2612 timer A/B), $00 = tick from the Z80 VBlank IRQ
The 68k side: game code writes $FFEE53 (music/priority slot) or $FFEE52 (SFX slot); routine $7FED8
(called once per frame by map/level/demo handlers) takes $EE53, else $EE52, and writes it to $A01C0A
under Z80 bus request ($7FF0E / $7FF2A).

    from qs_game_sound import play_sound, stop_sound, driver_state, enable_tick
    enable_tick(md)            # ONLY for this analysis DLL (YM2612 stubbed -> timers never fire)
    play_sound(md, 0x88); md.run(10); print(driver_state(md))

    python qs_game_sound.py    # verification: every music ID and SFX ID through the driver
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qs_game_data import R  # noqa: E402

MUSIC = {
    0x81: 'Title screen',
    0x82: 'Boss battle (Dracula, Viking ghost, tiger, Pete, Egypt boss; Pete kidnap event)',
    0x83: 'Duckburg',
    0x84: 'Mexico',
    0x85: "Maharajah (palace grounds / 'MAHARAJAH' in sound test)",
    0x86: 'Egypt (desert / Sphinx)',
    0x87: "Maharajah palace interior (sound test 'ROOM1')",
    0x88: 'Transylvania',
    0x89: 'South Pole',
    0x8A: 'Hideout (outside, room 0)',
    0x8B: "Ruins/dungeon: Mexico Aztec ruins, Egypt pyramid, Island rooms 0-2 (sound test 'ROOM2')",
    0x8C: 'Ending (data only present when the ending bank is loaded at Z80 $0F00)',
    0x8D: 'Staff roll (credits, mode $0A)',
    0x8E: 'Viking ship',
    0x8F: "Duckburg room 2 (Gyro's area)",
    0x90: 'Conversation (dialogue windows) / intro story',
    0x91: 'Game over',
    0x92: 'World map',
    0x93: 'Treasure room (Island, before the figurine)',
    0x94: 'Final boss (Island room 3)',
    0x95: 'Item get / boss defeated jingle',
    0x96: "Hideout interior (rooms 1-3; sound test 'THE ISLAND')",
    0x97: 'Continue? screen',
    0x98: '(unreferenced by game code)',
}

# SFX names from the 68k call sites ($FFEE52 writes); '(unreferenced)' = valid driver entry never requested
SFX_NAMES = {
    0xA0: 'jump', 0xA1: 'plunger shot', 0xA2: 'plunger sticks (wall)', 0xA3: 'plunger hits/bounces',
    0xA4: 'popcorn shot', 0xA5: 'plunger stuck on enemy', 0xA6: 'bubble-gum shot', 0xA7: 'object at $FAB6',
    0xA8: 'fire with no ammo (puff)', 0xA9: 'player action $65A2', 0xAA: 'player action $6B58',
    0xAD: 'jump variant ($7112)', 0xAE: 'Donald hurt', 0xAF: 'pause menu open', 0xB0: 'item pickup / extra life',
    0xB1: 'airplane (map departure)', 0xB2: 'pause cursor move', 0xB4: 'airplane drop-off at level start',
    0xB5: 'enemy ($EBDE)', 0xB7: 'enemy ($DCEC/$EBDE)', 0xB8: 'enemy stunned/defeated ($E022/$E224/$F936)',
    0xBB: 'enemy ($EBDE)', 0xBC: 'player action $64F4', 0xBD: 'enemy ($E688)', 0xBE: 'enemy ($EBDE)',
    0xBF: 'enemy ($EBDE)', 0xC0: 'boss/enemy ($F6BE/$11BDC/$13C2E)', 0xC1: 'door opens / rumble',
    0xC2: 'item appears', 0xC4: 'object $120E0', 0xC5: 'object $160F4', 0xC7: 'jump variant ($7112, $EE59)',
    0xC9: 'object $15D6C', 0xCB: 'object $10572', 0xCC: 'object $FDDA/$16896', 0xCD: 'object $16A40',
    0xD0: 'object $FBBA', 0xD2: 'object $10E78', 0xD3: 'object $11718', 0xD4: 'object $81F8/$10896',
    0xD5: 'ice melts (Scepter of Ra used)', 0xD6: 'final boss $18E3A', 0xD7: 'final boss $18E64',
    0xD8: 'final boss $18A70', 0xD9: 'final boss $18B14', 0xDA: 'object $19460', 0xDB: 'object $195D6',
    0xDF: 'object $11F0E', 0xE0: 'object $11E68', 0xE1: 'object $11D1E', 0xE2: 'map dipped in water',
    0xE3: 'object $17110/$183DE', 0xE4: 'object $18014',
    0xF0: 'quack attack (special SFX channel)',
}
COMMANDS = {0xF8: 'fade out music', 0xF9: 'stop all sound (driver reset $072F)', 0xFA: 'silence PSG',
            0xFB: 'stop special SFX ($F0 channel)', 0xFF: 'stop all sound'}

Q_SLOT = 0x1C0A
MUSIC_TRACKS = [0x1C40 + 0x30 * i for i in range(10)]     # 7 FM/DAC + 3 PSG
SPECIAL_TRACKS = [0x1E20, 0x1E50]
SFX_TRACKS = [0x1E80 + 0x30 * i for i in range(6)]


def _music_headers():
    tab = 0x7011B
    out = {}
    for i in range(24):
        p = R[tab + 2 * i] | (R[tab + 2 * i + 1] << 8)
        out[0x81 + i] = p
    return out


HEADERS = _music_headers()          # Z80 addresses ($8000+ = bank window, $0F00 = ending bank in Z80 RAM)


def enable_tick(md):
    """Analysis-DLL workaround: make the driver tick on the Z80 VBlank IRQ ($0038) because the stubbed
    YM2612 never raises timer flags.  The game rewrites $1C07=$80 whenever it reloads the driver ($7FE00),
    so call this every frame.  Not needed with a real YM2612."""
    md.zram[0x1C07] = 0


def load_bank(md, ending):
    """Upload the SFX bank ($0DD0) or the ending bank ($0F00) into Z80 RAM, like $7FE00/$7FE16."""
    z = md.zram
    if ending:
        z[0xF00:0xF00 + 0x6D8] = R[0x7A0C4:0x7A0C4 + 0x6D8]
    else:
        z[0xDD0:0xDD0 + 0xE1E] = R[0x792A6:0x792A6 + 0xE1E]


def play_sound(md, sid):
    """Queue sound/command `sid` exactly like the game's $7FED8 does (write to Z80 $1C0A).
    Safe between frames (the harness equivalent of the 68k's Z80 bus request)."""
    ending_loaded = bytes(md.zram[0xF00:0xF08]) == R[0x7A0C4:0x7A0C4 + 8]
    if sid == 0x8C and not ending_loaded:
        load_bank(md, True)                     # ending music lives in the ending bank (overwrites SFX)
    elif sid != 0x8C and ending_loaded and 0x81 <= sid < 0xF8:
        load_bank(md, False)                    # restore the SFX bank for everything else
    z = md.zram
    for slot in (Q_SLOT, Q_SLOT + 1, Q_SLOT + 2):   # driver scans $1C0A, then $1C0B/$1C0C
        if z[slot] == 0:
            z[slot] = sid
            return slot
    z[Q_SLOT] = sid
    return Q_SLOT


def play_sound_via_game(md, sid, music=True):
    """Alternative: let the game send it ($FFEE53 = priority/music slot, $FFEE52 = SFX slot).
    Only delivered in modes whose handler calls $7FED8 (map, level, demo, pause->resume)."""
    md.set8(0xFFEE53 if music else 0xFFEE52, sid)
    if music:
        md.set8(0xFFEE67, sid)                  # game's 'current music' (restored after dialogues)


def stop_sound(md, fade=False):
    play_sound(md, 0xF8 if fade else 0xF9)
    md.set8(0xFFEE67, 0)                        # as the game's own stop routine $95C0 does


def driver_state(md):
    z = bytes(md.zram)

    def active(lst):
        return [a for a in lst if z[a] & 0x80]
    mus = active(MUSIC_TRACKS)
    cur = None
    if mus:
        ptr = z[mus[0] + 3] | (z[mus[0] + 4] << 8)     # track data pointer (ix+3/ix+4)
        best = None
        for sid, h in HEADERS.items():
            if (h >= 0x8000) == (ptr >= 0x8000) and h <= ptr and (best is None or h > HEADERS[best]):
                best = sid
        cur = best
    return {'queue': z[0x1C0A:0x1C0D].hex(), 'starting': z[0x1C09], 'priority': z[0x1C18],
            'fade': z[0x1C0D], 'music_tracks': [hex(a) for a in mus], 'music_id': cur,
            'sfx_tracks': [hex(a) for a in active(SFX_TRACKS)],
            'special_tracks': [hex(a) for a in active(SPECIAL_TRACKS)], 'tick_mode': z[0x1C07]}


# =====================================================================================================
if __name__ == '__main__':
    from qs_game_common import new_md
    from qs_game_levelselect import start_level
    md = new_md()
    _run = md.run

    def ticking_run(n=1, buttons=None):          # keep the driver ticking during the whole boot
        if buttons is not None:
            md.hold(buttons)
        for _ in range(n):
            enable_tick(md); _run(1)
    md.run = ticking_run
    assert start_level(md, 0)                   # Duckburg, gameplay running (level entry requests $83)
    base = md.save()

    def step(n=1):
        md.run(n)

    # 1) the game's own requests reach the driver: level-entry music, then $EE53 from 68k code
    step(5)
    print('in-game (Duckburg) :', driver_state(md))
    play_sound_via_game(md, 0x92); step(8)
    print('via $FFEE53 = $92  :', driver_state(md))
    # 2) every music ID through play_sound
    bad = []
    for sid in range(0x81, 0x99):
        md.load(base); step(2)
        play_sound(md, sid); step(8)
        st = driver_state(md)
        ok = st['music_id'] == sid and st['queue'] == '000000'
        if not ok:
            bad.append(sid)
        print('music $%02X %-4s tracks=%d id=%s  %s' % (sid, 'OK' if ok else 'FAIL', len(st['music_tracks']),
                                                        st['music_id'] and hex(st['music_id']), MUSIC[sid]))
    # 3) every SFX slot $A0-$EF and $F0
    sfx_ok = []
    for sid in list(range(0xA0, 0xF0)) + [0xF0]:
        md.load(base); step(2)
        stop_sound(md); step(2)
        play_sound(md, sid); step(2)
        st = driver_state(md)
        if st['sfx_tracks'] or st['special_tracks']:
            sfx_ok.append(sid)
    print('SFX that start a track: %d/%d' % (len(sfx_ok), 81), ' not starting (empty entries):',
          [hex(s) for s in list(range(0xA0, 0xF0)) + [0xF0] if s not in sfx_ok])
    # 4) stop / fade
    md.load(base); step(2); play_sound(md, 0x88); step(5)
    before = driver_state(md)['music_tracks']
    stop_sound(md); step(2)
    print('stop: before %d tracks, after %d' % (len(before), len(driver_state(md)['music_tracks'])))
    play_sound(md, 0x88); step(5); stop_sound(md, fade=True); step(1)
    f1 = driver_state(md)['fade']; step(120)
    print('fade: counter %d -> %d, tracks after 120f: %s' % (f1, driver_state(md)['fade'],
                                                               driver_state(md)['music_tracks']))
    print('music failures:', [hex(b) for b in bad])
