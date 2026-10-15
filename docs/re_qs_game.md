# QuackShot Starring Donald Duck (World, REV 00): game logic RE

Game state machine, world and level structure, level select, cheats, sound and other PC-port hooks.
All addresses are 68000 addresses. RAM `$FFxxxx` is written `$xxxx` when it is unambiguous: the game uses
absolute-short addressing, e.g. `$8F96.w` = `$FF8F96`. ROM addresses are `$0xxxxx`.
Everything here was checked on the headless core (`tools/mdt.py`). The scripts are:

| file | purpose |
|---|---|
| `tools/re/qs_game_common.py` | boot helpers (`boot_to_menu`, `boot_to_map`), key RAM constants, screenshot grid |
| `tools/re/qs_game_data.py` | Kosinski decompressor (the game's trap #4), level entry table, debug stage table, dialogue text decoder |
| `tools/re/qs_game_levelselect.py` | `start_level(md, index)` from power-on; main starts all 48 entries and writes a grid |
| `tools/re/qs_game_cheats.py` | cheat RAM addresses, ROM patches, per-frame writes; main runs 11 tests |
| `tools/re/qs_game_sound.py` | sound IDs, `play_sound` / `stop_sound` / `driver_state`; main plays every ID through the driver |
| `tools/re/qs_game_z80dis.py` | small Z80 disassembler for the sound driver |
| `tools/re/qs_game_levelselect.png` | proof grid: every level-select entry loaded and playable |
| `tools/states/qs_game_map.state` | world map, idle, right after a fresh game start (frame 632) |

---------------------------------------------------------------------------------------------------------

## 1. Game state machine

### 1.1 Main loop (ROM `$48A`)
```
0048a  move.w #0,$FF18
00490  moveq #0,d0 / move.b $FF19,d0 / lsl #2 / movea.l $4AE(pc,d0.w),a0 / jsr (a0)   ; mode handler
004a0  jsr $9550                      ; wait for VBlank: $EE00=1, SR=$2500, spin until VBlank clears $EE00
004ac  bra $490
```
- **`$FF18.w` = game mode.** The low byte `$FF19` indexes the table at `$4AE`. Bit 15 (`bset #7,$FF18`)
  is a "mode initialised" flag: handlers 0, $0B, $0F and $10 run their init once (bit clear), then their
  per-frame part. Writing a new mode word (bit 15 = 0) to `$FF18` switches modes.
- Each handler call is one frame. Some modes (title, intro, ending, dialogue windows) run blocking loops
  that call `$9550` themselves.

| `$FF19` | handler | mode | enters next |
|---|---|---|---|
| $00 | `$455A` | SEGA logo (init clears RAM `$FF0000-$FFFCFF` via `$20D2`; sends $F9 to stop all sound) | $01 |
| $01 | `$A3D6` | title screen, blocking. Music $81. Waits `$8F7E`=360 frames. START goes to $0B; timeout sets `$8F7C`=5 and goes to $06 | $0B / $06 |
| $02 | `$9BE8` | world map. Sub-state `$A344`: 0 init, 1 select, 2 plane departure (`$A346`=180 frames or a button) | $03 |
| $03 | `$461C` | map to level: `$EE61`=0, power topped up to 5 (`$506C`). If `$8F40`==$2F, goes to ending | $04 / $09 |
| $04 | `$464C` | level load (location `$8F34`, entry `$8F74`). First load after the map: plane drop-in (`$A300`=$A1, pause lock `$8FA6`=$FF) | $05 |
| $05 | `$477A` | gameplay (see 1.3) | $08 / $04 / $02 / $0F |
| $06 | `$4974` | demo setup: `$8F7C`-- (<0 goes to mode 0); location, entry, weapon and plunger from tables at `$49DE`+ | $07 |
| $07 | `$4A0C` | demo play (`$EE50`=1: pad from recorded data). START goes to $00; end of data (`$EE6B`) goes to $06 | $06 / $00 |
| $08 | `$07AA` | pause / inventory menu (sub-state `$8FA4`) | $05 |
| $09 | `$A2D8` | ending cut-scene (loads the ending Z80 bank `$7FE16`, music $8C) | $0A |
| $0A | `$A0FC` | staff roll (music $8D) | $00 |
| $0B | `$4A5C` | START / OPTIONS menu. The OPTIONS screen (sound test, control) is handled by objects inside this mode | $0C |
| $0C | `$4FE2` | intro story (blocking `$AB3E`, music $90; START skips) | $0E |
| $0D | `$0502` | (table slot = checksum-fail code: red screen, infinite loop at `$520`) | - |
| $0E | `$4604` | new game / continue init `$5024` (see 2.6), music $92 | $02 |
| $0F | `$487E` | game over (music $91, `$89E0`=180 frames) | $10 |
| $10 | `$48D0` | "CONTINUE / STOP THE ADVENTURE" (music $97; cursor `$89E0` bit 0) | $0E / $00 |

Measured from power-on with no input: mode 0 at frame 0, init done at 56, title at 173. Pressing START on
the title at frame 1000 gave mode $0B. START in the menu gave $0C (intro), then $0E, then $02 (map). START
on the map gave $03, then $04, then $05. Skipping the intro reaches the map at frame about 632
(`boot_to_map`).

### 1.2 VBlank (vector $78 = `$52A`)
```
0052a  movem.l d0-a6,-(sp)
0052e  tst.b $EE00 / beq $77E          ; main loop not finished = lag frame: do nothing (not even pad read)
00536  clr.b $EE00
0053e  bclr #0,$EE02 / beq            ; palette upload request: DMA $F580 -> CRAM
00576  move.w $FF1A,d0 ; jmp $582(pc,d0*4)
```
| `$FF1A` | code | used by | work |
|---|---|---|---|
| 0 | `$59A` | SEGA, map, map to level | sprite DMA ($F300 to VRAM $D400), scroll/plane update `$2890` |
| 1 | `$5D2` | level (set in mode 4) | `$5FB6` queued VDP command, then everything in 2 |
| 2 | `$5D6` | game over | sprite DMA, `$2890`, DMA queue `$41C4` (list at `$FA90`), HUD `$D8E` |
| 3 | `$618` | title, intro, ending | sprite DMA, VRAM fill if `$FC00`, line/cell scroll from `$8520/$8524/$8560/$8564` |
| 4 | `$76C` | pause, blocking screens | only the common tail |
| 5 | `$732` | START/OPTIONS menu | sprite DMA, `$4F4C` |

Common tail at `$76C`: pad read `$2102`; `$8F28` countdown (word, stops at 0); `$8F2A` frame counter (word, +1).
The pause saves and restores `$FF1A` in `$8F8A`.

### 1.3 Gameplay mode $05 (`$477A`)
Each frame it calls: `$1018` (pending dialogue message `$8FBC`), `$7FED8` (send sound request), object
update, camera and scroll. Then it checks these, in order:
- `$EE4F` set: restart the level. Commits pending score and goes to mode 4.
- `$EE58` set (death): pending score added; `subq.w #1,$8F98` at `$481E`. If lives reach 0, mode $0F.
  Otherwise power `$8F96`=5 and mode 4 (reload at the checkpoint entry `$8F74`).
- `$EE5F` set (left by airplane): items committed `$EEE0` to `$FCA0` (`$4870`), score committed, mode 2.
- Pause: when `$8FA6` (pause lock, counts down) is 0 and `$EE5A` is set (player code: START pressed,
  `sne $EE5A`), `$8FA4`=0 and mode 8.

### 1.4 Other useful globals
| addr | size | meaning |
|---|---|---|
| `$FF0C` | b | copy of version port `$A10001`: bit 7 = overseas, bit 6 = PAL |
| `$FF1C` | b | overseas flag (`sne` of `$FF0C` bit 7). Picks English text (trap4 $4E, ROM `$7BF00`) or Japanese ($4F, `$7CC30`) |
| `$FF08/$FF0A` | w | VDP reg 1 values for display off / on (NTSC or PAL 30-cell) |
| `$FF1E` | w | control layout 0-4 (OPTIONS). Cleared only at power-on / reset (`$FFFF00` block) |
| `$EE00` | b | VBlank handshake (main loop sets it, VBlank clears it) |
| `$EE02` | b | bit 0 = upload palette `$F580` to CRAM this VBlank |
| `$8F2A` | w | frame counter |

---------------------------------------------------------------------------------------------------------

## 2. World, locations, levels, progression

### 2.1 Locations (`$8F34.w`)
Names are the map-label table at ROM `$4DA8` (12 words each; font: `0`-`9`=$01-$0A, `A`-`N`=$0B-$18,
`P`-`Z`=$19-$23; there is no `O`, the digit 0 is used instead; space = $3B).

| `$8F34` | name | rooms (`$8F36`) | entry points (`$8F74`) | music |
|---|---|---|---|---|
| 1 | DUCKBURG | 0-2 | 7 | $83, room 2 $8F |
| 2 | MEXICO | 0-3 | 8 | $84, room 3 (Aztec ruins) $8B |
| 3 | TRANSYLVANIA | 0-6 | 9 | $88 (room 6 = Dracula: $82 until beaten) |
| 4 | VIKING SHIP | 0-4 | 8 | $8E (room 4 = ghost boss: $82 until beaten) |
| 5 | SOUTH POLE | 0-3 | 8 | $89 |
| 6 | MAHARAJAH | 0-8 | 11 | $85 grounds, $87 palace rooms 1-7, room 8 = tiger $82 |
| 7 | EGYPT | 0-5 | 11 | $86 desert, $8B pyramid rooms 1-5 |
| 8 | HIDEOUT (Pete's) | 0-4 | 5 | $8A room 0, $96 rooms 1-3, $82 room 4 (Pete) |
| 9 | THE ISLAND (Great Duck Treasure) | 0-3 | 4 | $8B rooms 0-2, $94 room 3 (final boss), $93 treasure |
| 10/11 | "ROOM1"/"ROOM2" | - | - | names only, used by the OPTIONS sound test |

**Entry-point records.** Trap #4 resource $4C is Kosinski data at ROM `$7EE26`, unpacked to RAM `$A400`
during each level load (`$350E`). `$3EF8` reads the record for `(location, entry)`; each record is 16 bytes:
`room.w, planeA_Y.w ($8520), planeA_X.w ($8524), planeB_Y.w ($8560), planeB_X.w ($8564), playerY.w ($A2D4),
playerX.w ($A2D0), $8F3A.b (facing), music.b`. If the music byte differs from `$EE67`, the driver is
reloaded (`$7FE00`) and the music requested. `python tools/re/qs_game_data.py entries` dumps all of them.
Entry indices past the first per-room ones are return points and checkpoints. Objects write `$8F74` as
Donald progresses; death reloads at the current `$8F74`.

**Room init / per-room script.** ROM `$35DE`: a table of long pointers per location, each to a word table
per room. The room init sets `$EF48` = the per-frame room script (e.g. Transylvania room 0 `$2A34`,
South Pole room 3 `$2D6E` = Pete's kidnapping).

**Return entry.** Mode 4 (`$46C8`): if the location is cleared (`$EEBF+loc` set) and the entry equals
`$475C[loc]` (loc 1:0, 2:2, 3:0, 4:2, 5:0, 6:0, 7:0), then `$8F74` = `$4766[loc]`
(5,6,7,7,7,10,10). The map (`$9D72`) does the same when departing: entry 0, or the return entry if the
location is cleared.

### 2.2 World map (mode 2, `$9BE8`)
- `$A344`: 0 init, 1 choose, 2 departure. `$8F34` is re-read every frame by `$9F0C`, which sets the plane
  marker from `$A03C` (early 3-location map) or `$9FAC` (full map, used when `$EEF3` is set) and draws the
  name label.
- D-pad (`$9EDA`): each location's 16-byte record = marker X/Y plus 6 pairs of (direction mask,
  destination). With `$EEF3`=0 only Duckburg, Mexico and Transylvania are linked. With `$EEF3` set the
  table covers locations 1-7.
- A/B/C/START (`$8F2D & $F0`) departs: `$A344`=2, `$9000`=$B1 plane object, `$8F74`=0 or return entry.
- Forced destinations (no input): `$EEFA && $EEFB` → The Island (`$8F34`=9, `$8F40`=$2B). `$EEFA` alone
  → the map waits (the map-dip object `$117E6` sets `$EEFB`, shows msg $1D, plays SFX $E2). `$EEF9` →
  Hideout (`$8F34`=8, `$8F40`=$26).

### 2.3 Progression / item flags (bytes, $FF = set)
Working copy `$EEE0-$EEFF`. Committed copy `$FCA0-$FCBF` is written when leaving by airplane (`$4870`) and
restored by new game / continue (`$5046`). `$EEC0-$EEC6` is outside the copied block. Setters and users
were found with xrefs and the object handlers; the dialogue texts come from the decoded message table (2.5).

| flag | meaning | set by | used by |
|---|---|---|---|
| `$EEC0+loc-1` (loc 1-7) | location cleared: plane pickup flag exists, return entry used | `$91FE(loc)` then `$926E`. Duckburg: Hero-Key NPC talk; Mexico: senorita talk; Transylvania: X ≥ $F40 (castle, msg $3C); Viking: captain talk; South Pole: key-in-ice; Maharajah: X ≥ $F20 (palace, msg $3D); Egypt: sphinx door | `$7FAA` plane pickup object; `$46C8`; `$9D80` |
| `$EEC8` | location being cleared (temp) | `$91FE` | `$926E` |
| `$EEE0` | **Hero Key** (pause item 0) | Duckburg NPC `$8DBC` (needs `$EEEF`), msg $00/$25 | USE at Mexico door |
| `$EEE1` | **Viking Key** (item 1) | South Pole ice `$8E82` after the Scepter is used, msg $22 | Viking ship door `$889E`, captain `$8A60` |
| `$EEE2` | **Sphinx Tear** (item 2) | Maharajah `$8B7E` (needs `$EEEE`), msg $17/$26 | USE at Egypt door |
| `$EEE3` | **Scepter of Ra** (item 3) | Egypt `$8F4E`, msg $19 | USE on the ice at the South Pole |
| `$EEE4` | **note "1 SUN 2 MOON 3 STAR" + red plunger** (item 4) | Goofy in Mexico ruins `$8D4E`, msgs $08/$3E; also sets `$EE48`=1 and entry 7 | `$5052` plunger type |
| `$EEE5` | **Viking Diary** (item 5) | South Pole `$8FA2` (msg $13); Hideout after Pete `$1730C` (msg $1A) | cleared by kidnapping `$2E08`; LOOK: `$F80` |
| `$EEE7` | bubble-gum weapon unlocked | Gyro `$8BFA` (+ `$8F92`=50, msg $04/$05/$28) | pause weapon slot 2, HUD |
| `$EEEC` | Mexico ruins door open | door `$8386` after USE Hero Key | door object, senorita |
| `$EEED` | green plunger (Viking ghost boss beaten) | boss `$197A6`, reward `$8246` | `$5052` (type 2), captain |
| `$EEEE` | tiger beaten | reward object `$8246` | Maharajah `$8B62` |
| `$EEEF` | met the senorita | senorita object init `$8C76` | Hero-Key NPC |
| `$EEF0` | Transylvania castle reached (msg $3C shown) | `$2A44` | |
| `$EEF1` | Transylvania wall blown up with bubble gum | `$821A` | ghost NPC `$8984` |
| `$EEF2` | Maharajah palace reached (msg $3D shown) | `$2E2A` | |
| `$EEF3` | real treasure map (Dracula beaten): full world map | `$15FA6` (msg $0D), `$8246` | map `$9C84/$9F22`, room init `$3914` |
| `$EEF5` | Egypt sphinx door open | `$8420` after USE Sphinx Tear | |
| `$EEF7` | met Gyro | `$8C4C` | |
| `$EEF8` | diary recovered from Pete | `$17308` | diary LOOK `$F80` (else msg $14 "too dark") |
| `$EEF9` | go to Hideout (after kidnapping) | `$2E0C` | map `$9D34`; cleared by South Pole room 3 init `$3AEE` |
| `$EEFA` | diary read (LOOK after `$EEF8`, msg $1B) | `$F98` | Hideout room 4 script `$3138` (msg $1C, exit), map |
| `$EEFB` | map dipped in water | map object `$11854` (msg $1D) | map: go to Island |
| `$EEFC` | Viking ship door open | `$88B8` after USE Viking Key | |
| `$EED0-$EED3` | level temp: key / tear / scepter was used (door or ice reacts) | pause USE `$FA8-$FEC` | door objects |
| `$EED6-$EED9` | level temp: Donald is at the Mexico door / Viking door / Egypt door / ice | objects | pause USE |
| `$EE48` | plunger type 0 yellow, 1 red (climb walls), 2 green (grab flying things) | `$5052` (from `$EEED`/`$EEE4`), `$8D52`, `$8AF2` | `$50B2` shot object $60/$61/$62. Colour `$F594` from `$501E` |
| `$EEE6`, `$EEE8-$EEEB`, `$EEF4`, `$EEF6`, `$EEFD-$EEFF` | no code references (unused) | | |

**Story order**, from the dialogue text and the checks above:
1. Duckburg: an NPC says you need climbing tools (loc 1 cleared).
2. Mexico: senorita (`$EEEF`) says you need the Hero Key from Duckburg (loc 2 cleared).
3. Duckburg: Hero Key.
4. Mexico: USE the key at the door, enter the Aztec ruins; Goofy gives the note and the red plunger and
   says Gyro is looking for you.
5. Duckburg: Gyro gives bubble gum.
6. Transylvania: blow up the wall, beat Dracula Duck, get the real treasure map; the full world map opens.
7. Maharajah: beat the tiger, get the Sphinx Tear.
8. Egypt: USE the Tear at the sphinx door, get the Scepter of Ra.
9. South Pole: USE the Scepter on the key frozen in ice, get the Viking Key.
10. Viking ship: USE the Viking Key at the door, beat the ghosts, get the green plunger.
11. South Pole: fly with the green plunger to the Viking Diary; Pete kidnaps the nephews and takes the
    diary (`$EEF9`).
12. Hideout: beat Pete, get the diary back (`$EEF8`).
13. LOOK at the diary (`$EEFA`); the Hideout water room dips the map (`$EEFB`).
14. The Island: final boss, treasure room (music $93); the figurine (msg $1F) sets mode 9, the ending.

### 2.4 Other level RAM
| addr | meaning |
|---|---|
| `$8F36` w | room within location |
| `$8F40` w | debug stage number 0-$2F. Only written by the map ($26 Hideout, $2B Island) and dead code. $2F makes mode 3 go to the ending |
| `$A2C0` | player object ($40 bytes): +$02 state (bit 15 = new), `$A2D0` X, `$A2D4` Y (pixels), +$18/+$1C velocity |
| `$8520/$8524` | camera: plane A Y / X. `$8560/$8564` plane B |
| objects | slots of $40 bytes at `$9000` (x48), `$9C00` (x11), `$9EC0` (x16), `$A2C0` player, `$A300` airplane. Word 0 = type; handler table ROM `$9672` + 4*type (e.g. $40 Gyro, $41 senorita, $42 Hero-Key NPC, $A4 Mexico door, $A9 Goofy, $AD Egypt door, $A1/$A2 airplane) |
| `$EE5E` | cut-scene active: the pad comes from `$8FA0` (script) and dialogues wait on `$8FA3` |
| `$EE5C` | Donald is at the plane pickup flag: CALL THE AIRPLANE allowed |
| `$8FBC` w | pending dialogue message id (`$1006`/`$1012`), shown by `$1018` |

### 2.5 Text and dialogue
Messages are trap4 $4E (ROM `$7BF00`, English), unpacked to `$CE00`: offset table, then pages; the first
byte of a page is the speaker. `qs_game_data.messages()` decodes all 63 messages, for example: 00 Hero Key
dialogue, 02/03 climbing tools and Goofy, 04 Gyro and bubble gum, 06 senorita, 08 Goofy's note and red
plunger, 0D real treasure map, 0E-10 Viking captain and green plunger, 11 key frozen in ice, 15 Pete's
ransom, 16/17 tiger challenge and Sphinx Tear, 1B diary text, 1D "go to the Great Duck Treasure Island",
1F figurine, 32-37 LOOK texts, 38 "can't communicate with the airplane", 39 "can't use it here",
3A "Donald used it...". Messages 29-31 are empty or developer placeholders ("MESSAGE NO47"...).

### 2.6 New game, continue, game over
`$5024` (mode $0E) runs for both a new game and a continue:
- lives `$8F98`=3; score `$EF34`=0, pending `$8F7A`=0; weapon `$8F66`=0; temper `$8F9A`=0; popcorn `$8F90`=0
- `$8F8E`=99 (never read)
- items `$FCA0` copied to `$EEE0`; `$EE48` recomputed

Power `$8F96` is 5 at location start. A new game starts clean because mode 0 cleared `$FCA0`.
Continues are unlimited. Verified: 3-1 with lives 1, forced death, then mode $0F, then $10, START, then map
at TRANSYLVANIA with lives 3, score 0, `$EEE7` kept and plunger type 1.

### 2.7 Built-in debug leftovers (no normal way to reach them)
- **Stage-select routine `$9E06`**: LEFT/RIGHT on `$8F2D` cycles `$8F40` 0..$2F and prints a 3-character
  label from `$A06C`. It is never called (no caller, no pointer). Its data tables are also unreferenced:
  `$9DA6` (stage to location), `$9DD6` (stage to entry), `$A06C` (labels "1-1".."9-B", "END").
  `qs_game_data.debug_stage_table()`; the level select below uses this list.
- **`$8F40`==$2F check in mode 3** (`$4634`): still works and starts the ending.
- **Free-move mode `$EE07`**: `$608E` player routine, `tst.b $EE07 / bne $6006`. The D-pad moves Donald
  4 px/frame with no gravity or collision; A+START leaves (`$6006`). Nothing in the ROM sets it, and level
  load clears it (`$4668`). Setting `$EE07`=$FF enables it (tested, 4.2).
- No button-code cheats: the title only checks START and the options screen only the D-pad and START.
  There is no SRAM (the header has no "RA" record) and no password system.

---------------------------------------------------------------------------------------------------------

## 3. Level select (verified)

`tools/re/qs_game_levelselect.py`, `start_level(md, index)`. `LEVELS` is the game's own 48-entry debug
stage list (label, location, entry), plus a story-flag preset for each entry.

### 3.1 Technique
1. **Boot:** power-on, wait for mode 1, press START until mode $0B. Wait 60 frames and press START (menu
   cursor is on START), which gives mode $0C. Press START until mode is not $0C (skips the intro); the game
   reaches mode 2. Wait until `$A344`==1 (map idle). Takes about 630 frames.
2. **On the idle map**, write:
   - all story flags `$EEC0-$EEFF` from the preset, and copy `$EEE0-$EEFF` to `$FCA0-$FCBF` so a continue keeps them
   - `$EE48` plunger type, plus `$F594` = `[$02CE,$022E,$02A2][type]` and `$EE02`=1 (what `$5000` does)
   - `$8F92` gum ammo (50 once Gyro has been met)
   - `$8F34` = location
   Run 2 frames so the marker and label update.
3. **Press START** (normal departure: plane, SFX $B1, music stop). For the Hideout and Island the flags
   (`$EEF9`, `$EEFA+$EEFB`) make the map leave by itself, as in the real game.
4. **First frame with mode 3:** write `$8F74` = entry (and `$8F34` again). Mode 3 runs, then mode 4 loads
   from these.
5. Wait for mode 5 and 200 frames for the plane drop-in.
- A location's cleared flag is not written when the entry equals `$475C[loc]`. Otherwise mode 4 swaps in the
  return entry (2.1).
- **END:** idle map, write `$8F40`=$2F, press START. Mode 3 goes to mode 9 (ending), then mode $0A
  (staff roll) about 2100 frames later.
- A faster, non-canonical variant skips the flight: write `$8F34`/`$8F74` and `$FF18`=3 on the idle map.
  This also skips the plane SFX and the `$95C0` music stop.

### 3.2 Presets (cumulative, in story order)
| preset | adds |
|---|---|
| new game | nothing |
| Mexico 1st | cleared Duckburg |
| Duckburg for Hero Key | cleared Mexico, `$EEEF` |
| Mexico ruins | `$EEE0` Hero Key |
| Duckburg Gyro | `$EEEC`, `$EEE4` note/red plunger, `$EE48`=1 |
| Transylvania | `$EEE7`, `$EEF7`, gum 50 |
| Maharajah | cleared Transylvania, `$EEF0`, `$EEF1`, `$EEF3` |
| Egypt | cleared Maharajah, `$EEF2`, `$EEEE`, `$EEE2` |
| South Pole (Viking key) | cleared Egypt, `$EEF5`, `$EEE3` |
| Viking ship | cleared South Pole, `$EEE1` |
| South Pole (diary) | cleared Viking ship, `$EEFC`, `$EEED`, `$EE48`=2 |
| Hideout | `$EEF9` (diary not held) |
| Island | `$EEF8`, `$EEE5`, `$EEFA`, `$EEFB`; `$EEF9` removed |

Entries use: Duckburg 1-1/1-2 new game, 1-3 Gyro; Mexico 2-1/2-2 Mexico 1st, 2-C/2-3 Mexico ruins;
Transylvania; Viking ship; South Pole (diary); Maharajah; Egypt; Hideout; Island; END new game.

### 3.3 Level list and verification
Run `python tools/re/qs_game_levelselect.py`. Result: all 48 OK, and every playable entry passed the
movement check (Donald's X changes with RIGHT/LEFT). The grid is `tools/re/qs_game_levelselect.png`.

| idx | label | loc | entry | room | idx | label | loc | entry | room |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 1-1 | 1 | 0 | 0 | 24 | 6-C | 6 | 8 | 1 |
| 1 | 1-2 | 1 | 1 | 1 | 25-30 | 621-626 | 6 | 1-6 | 2-7 |
| 2 | 1-3 | 1 | 3 | 2 | 31 | 6-B (tiger) | 6 | 7 | 8 |
| 3-6 | 2-1,2-2,2-C,2-3 | 2 | 0,1,2,3 | 0-3 | 32-37 | 7-1..7-4,7-C,7-5 | 7 | 0-5 | 0-5 (7-5 = mine cart) |
| 7-13 | 3-1,3-2,3-3,341,342,343,3-B | 3 | 0-6 | 0-6 | 38-42 | 8-1..8-4,8-B (Pete) | 8 | 0-4 | 0-4 |
| 14-18 | 4-1,4-2,4-C,4-3,4-B (ghosts) | 4 | 0-4 | 0-4 | 43-46 | 9-1..9-3,9-B (final) | 9 | 0-3 | 0-3 |
| 19-22 | 5-1..5-4 | 5 | 0-3 | 0-3 | 47 | END | - | - | mode 9, then $0A |
| 23 | 6-1 | 6 | 0 | 0 | | | | | |

(Palace rooms 621-626 report `$8F74`=8 in gameplay because a room object rewrites the checkpoint. The room
is correct.)

**Story-gate check.** `verify_mexico_gate()` returns True: `key_used=$FF`, `door_open=$FF`, room 3,
music $8B. It loads 2-C with the "Mexico ruins" preset and plays it:
1. The senorita now says msg $07 ("use the Hero Key") instead of $06, and the cleared cut-scene runs.
2. Donald walks to the door (`$EED6` set), pauses, and USEs the Hero Key: "DONALD USED IT...", `$EED0`=$FF.
3. The door opens (`$EEEC`=$FF).
4. Walking into the doorway enters room 3 (Aztec ruins, music $8B).

---------------------------------------------------------------------------------------------------------

## 4. Cheats

### 4.1 RAM
| addr | size | meaning |
|---|---|---|
| `$8F96` | w | **power** (hearts) 0-8. HUD shows 8 slots; `$D8E` draws `8-$8F96` empty. New life = 5; mode 3 raises it to at least 5; small heart +1 (at 8: +100 pts); big heart = 8 (at 8: +200 pts) (`$7D48/$7D66`) |
| `$8F98` | w | **lives** ("Donald"), binary, capped at 99 by the 1-up `$7CEA` |
| `$8F9A` | w | **temper** 0-5, one per chilli pepper (`$7D00`). At 5: quack attack. Reset to 0 when quack attack ends and at level load if ≥5 (`$4088`) |
| `$EE62` / `$8FA8` | b / w | **quack attack** active / frames left (360). While active, enemy contact doesn't set `$EE09`; enemies die. At the end (`$5FD8`): temper 0, SFX $FB, 120-frame blink |
| `$8F90` | w | **popcorn** 0-95. Shot costs 5 (`$5108`); pickup +5 (at 95: +100 pts) |
| `$8F92` | w | **bubble gum** 0-50. Shot costs 1 (`$5130`); pickup +5; Gyro refills to 50 |
| `$8F66` | w | **weapon** 0 plunger, 1 popcorn, 2 bubble gum (pause menu; 2 needs `$EEE7`) |
| `$EE48` | b | plunger type 0/1/2 (2.3) |
| `$EE09` | b | hit request, set on contact by `$742C` / `$B866` / `$F414` when `$EE08`==0 and `$EE62`==0 |
| `$EE08` / `$EE0A` | b / b | post-hit invulnerability flag / timer ($78 = 120 frames, from `$6AA8` after the hurt animation). `$5E40` counts down and blinks |
| `$8FAA` | w | extra damage for the next hit (bosses); damage = 1 + `$8FAA` (`$5E00`) |
| `$EE58` | b | death request (set at 0 power `$6A94`, falling off the screen `$6BF6`, scripted deaths `$2B6C`, `$304E`) |
| `$EF34` | l | **score / 10**, binary (not BCD). HUD `$EEE` prints `$EF34*10` as 8 digits |
| `$8F7A` | w | pending points (/10), moved into `$EF34` 10 per frame (+100 shown). Extra life when the shown score passes 100,000, then every 400,000 (`$DFC`, SFX $B0) |
| `$EE07` | b | debug free-move (2.7) |

### 4.2 Cheat implementations
- **ROM patches** (`PATCHES` in `qs_game_cheats.py`, applied with `md.patch16`). The ROM checksum is
  checked at boot (`$3EC`: sum of words `$200`-end compared with header `$18E`). Either apply the patches
  after boot, or also write the new sum to `$18E` (tested: patched ROM before boot hangs at `$520` with a
  red screen; with the fixed checksum it boots). `$18E` itself is outside the summed range.
- **Per-frame writes** go between frames while mode is $05 or $08.

| cheat | method | exact change | test (`python tools/re/qs_game_cheats.py`) |
|---|---|---|---|
| infinite lives | ROM | `$481E`: `5378 8F98` (subq.w #1,$8F98) → `4A78 8F98` (tst.w $8F98). The next `beq $483E` (game over) still sees lives≠0. Replacing with NOPs is wrong: the beq then tests the flags of an earlier `move #0` and always goes to game over | forced death: normal 3→2; patched 3, mode 5. With lives=1: mode 5, lives 1 (no game over) — PASS |
| infinite lives | RAM | `$8F98`=9 each frame (death handler decrements, the next write restores) | — |
| infinite health | ROM | `$5E16`: `9F78 8F96 5378 8F96` → `4E71` x4. Hits still register and knock back, and the invulnerability timer runs | 3-1 walk right: 2 hits, power stays 5 (control: 5→3) — PASS |
| infinite health | RAM | `$8F96`=8 each frame | 2 hits, min power 8 — PASS |
| invincibility | ROM | `$742C`, `$B866`, `$F414`: `50F8 EE09` (st.b $EE09) → `4A38 EE09` (tst.b). No hit is ever registered: no damage, no knock-back, no blink. Pits and scripted deaths (`$EE58`) still kill | 0 hits, power 5, 0 knock-back frames — PASS |
| infinite ammo | ROM | `$5108`: `5B78 8F90` → `4E71 4E71` (popcorn); `$5130`: `5378 8F92` → `4E71 4E71` (gum). Firing still needs ammo >0 (`$518C`/`$51B4`), so give some once | normal: popcorn 20→10, gum 20→19; patched 20/20 — PASS |
| infinite ammo | RAM | `$8F90`=95, `$8F92`=50 each frame | — |
| all weapons / abilities | RAM | `$EEE7`=$FF (bubble gum), `$EEE4`=`$EEED`=$FF, `$EE48`=2 and palette `$F594`=`$02A2`, `$EE02`=1. For only red: type 1, `$022E` | pause menu shows BUBBLE GUM; green plunger in HUD — PASS |
| quack attack always on | RAM | `$EE62`=$FF, `$8FA8`=360, `$8F9A`=5 each frame | 0 hits, power 5, enemies killed on contact (+4000 pts) — PASS |
| quack attack never ends | ROM | `$5FDE`: `5378 8FA8` → `4E71 4E71` (timer frozen once started) | normal: ends after 360 frames with temper 0 and blink; patched: still on after 720 frames — PASS |
| never lose temper | RAM | `$8F9A`=4 each frame (next chilli starts quack attack) or 5 | — |
| free move / noclip | RAM | `$EE07`=$FF (A+START or level reload ends it) | UP 30 frames: Y 160→40, stays 40 with no fall — PASS |
| score | RAM | `$EF34` long = score/10 | — |

---------------------------------------------------------------------------------------------------------

## 5. Sound

### 5.1 Driver and request path
- **Z80 driver:** Sega "SMPS Z80" (same design as Sonic 2's). `$7FE00` loads it: holds Z80 bus and reset,
  copies ROM `$784E0` ($DC6 bytes) to Z80 `$0000` and ROM `$792A6` ($E1E bytes, SFX data) to Z80 `$0DD0`,
  then writes 12 bytes at Z80 `$1BFE`: `07 80 07 00 00 80 B4 00 E6 80 00 00`. That is: bank $0E
  (ROM `$70000-$77FFF`), master pointer `$8000`, timer A $0B4, timer B $E6, `$1C07`=$80 (timer-driven).
  `$7FE16` is the ending variant: it copies ROM `$7A0C4` ($6D8 bytes) to Z80 `$0F00` instead (music $8C
  lives there). It is used by mode 9. The game reloads the driver whenever the level music changes
  (`$3F58`), on the menu, the title and the staff roll.
- **Pointer table at bank `$8000`** (ROM `$70000`, little-endian Z80 pointers):
  priority table `$809C` (ROM `$7009C`, indexed by ID-$81), special-SFX table `$0E70` (Z80 RAM),
  music table `$811B` (ROM `$7011B`, $81-$98), SFX table `$0DD0` (Z80 RAM, $A0-$EF).
- **68000 side:** code writes `$EE53` (music, or anything that must win) or `$EE52` (SFX). `$7FED8`, called
  once per frame by map, level, demo and the dialogue routine, sends `$EE53` if non-zero, else `$EE52`, to
  Z80 `$A01C0A` under bus request (`$7FF0E`/`$7FF2A`) and clears the slot. `$EE67` = game's current music
  ID; it is restored after dialogues (`$105A`) and zeroed by `$95C0` (stop: sends $F8). `$EE76`: dialogue
  music override (0 = $90 conversation, 1 = keep current music, bit 7 set = that ID, e.g. $95 item jingle).
  `$EE77` = don't restore music after the dialogue.
- **Z80 side:** the main loop (`$07C7`) scans `$1C0A`, then `$1C0B`, `$1C0C` (only while earlier slots are
  non-empty). An ID is accepted into `$1C09` if its priority ≥ `$1C18`; the slot is cleared. The tick
  (`$00F9`, run on YM timer overflow when `$1C07`=$80, or from the Z80 VBlank IRQ `$0038` when `$1C07`=0)
  handles `$1C09` in `$0456`:
  - $81-$9F: music (`$04A9`)
  - $A0-$EF: SFX (`$0553`)
  - $F0-$F7: special SFX (`$054A`)
  - $F8-$FB: commands (table `$0480`)
  - $FF: stop all
- **Z80 RAM state:**
  - music tracks at `$1C40` + n*$30 (n=0-9: 7 FM/DAC, then PSG at `$1D90`/`$1DC0`/`$1DF0`); byte 0 bit 7 =
    playing; +3/+4 = data pointer
  - special SFX tracks `$1E20`/`$1E50`; SFX tracks `$1E80` + n*$30 (6)
  - `$1C0D/$1C0E/$1C0F` fade counters; `$1C10` pause flag (never used by this game); `$1C13/$1C14` tempo;
    `$1C18` current priority
- **Pause:** the game does not pause the music driver. The pause only plays SFX $AF, and the music keeps
  going. Verified in Transylvania with the tick enabled: in mode 8, music $88 still has 8 tracks active,
  the FM1 pointer keeps moving over 120 frames, and `$1C10` stays 0.
- **Emulator note:** this DLL stubs the YM2612, so its timer flags never set and the driver never ticks.
  Requests are accepted into `$1C09` but nothing starts. To check the driver, `enable_tick(md)` writes
  `$1C07`=0 every frame, which moves the tick onto the Z80 VBlank IRQ. With a real YM2612 core (the port)
  this is not needed.

### 5.2 Music IDs (24 valid: $81-$98)
Header addresses are Z80 bank addresses (bank $0E; ROM = `$70000` + addr - `$8000`).

| ID | use | header |
|---|---|---|
| $81 | title screen (`$AFEC`) | $814B |
| $82 | boss (Dracula room init `$391A` until `$EEF3`; Viking ghost `$3A0C` until `$EEED`; tiger, Pete, Egypt boss `$2FBE`; kidnapping `$2DB8`) | $8C8A |
| $83 | Duckburg (demo 1) | $931E |
| $84 | Mexico (demo 2) | $9E7E |
| $85 | Maharajah grounds | $A37D |
| $86 | Egypt desert | $AA54 |
| $87 | Maharajah palace interior (sound test "ROOM1") | $B0F5 |
| $88 | Transylvania (demo 3) | $B407 |
| $89 | South Pole | $B983 |
| $8A | Hideout room 0 | $C04C |
| $8B | ruins / dungeon: Mexico room 3, Egypt pyramid, Island rooms 0-2 (sound test "ROOM2") | $C862 |
| $8C | ending cut-scene (Z80 RAM `$0F00`, ending bank only) | $0F00 |
| $8D | staff roll | $CDCF |
| $8E | Viking ship | $DCB1 |
| $8F | Duckburg room 2 (Gyro area; also `$8514`) | $E159 |
| $90 | dialogue / intro story | $E6D2 |
| $91 | game over | $E8C9 |
| $92 | world map | $EA08 |
| $93 | Island treasure room (`$186C8`) | $ED12 |
| $94 | final boss (Island room 3) | $F052 |
| $95 | item get / boss defeated jingle | $F5E4 |
| $96 | Hideout rooms 1-3 (the sound test labels it "THE ISLAND") | $F733 |
| $97 | continue screen | $F918 |
| $98 | never requested by game code | $F9FE |

### 5.3 SFX and commands
- **Valid SFX:** $A0-$E6 (71 entries). $E7-$EF all point at the end of the SFX bank (`$1BEE`) and play
  nothing. $F0 is the only special SFX (quack attack; stopped by $FB).
- **Commands:**
  - $F8: fade out music (`$06B8`: DAC and PSG stop at once, FM fades over about 80 ticks)
  - $F9 / $FF: stop everything (`$072F`: driver RAM cleared, YM/PSG silenced)
  - $FA: silence PSG
  - $FB: stop special SFX
  - $80: no-op
  - $F1-$F7 and $FC-$FE: invalid, never send them. They read past their tables, and $FC-$FE jump into data.
- **SFX names**, from game call sites (`SFX_NAMES` in `qs_game_sound.py`):

| ID | sound | ID | sound |
|---|---|---|---|
| $A0 | jump | $AF | pause menu |
| $A1 | plunger shot | $B0 | item / 1-up |
| $A2 | plunger sticks to a wall | $B1 | airplane departs (map) |
| $A3 | plunger bounces | $B2 | pause cursor |
| $A4 | popcorn shot | $B4 | airplane drop-in |
| $A5 | plunger on an enemy | $B8 | enemy stunned / defeated |
| $A6 | gum shot | $C1 | door opens / rumble |
| $A8 | out-of-ammo puff | $C2 | item appears |
| $AD / $C7 | jump variants | $D5 | ice melts |
| $AE | Donald hurt | $E2 | map dipped in water |
| $F0 | quack attack | | |

  The rest are tagged with the ROM address of the object that plays them.

### 5.4 OPTIONS sound test and controls
- **Mode $0B OPTIONS screen:** SOUND TEST, CONTROL, EXIT. START exits.
- **Sound test:** LEFT/RIGHT step `$9032` (the options object's +$32) in steps of $18 through the 11 map
  names at `$4DA8`. Each step plays `$4C20[i]` through `$EE52`:
  83 DUCKBURG, 84 MEXICO, 88 TRANSYLVANIA, 8E VIKING SHIP, 89 SOUTH POLE, 85 MAHARAJAH, 86 EGYPT,
  8A HIDEOUT, 96 THE ISLAND, 87 ROOM1, 8B ROOM2. There are no SFX in the sound test.
- **CONTROL** (`$FF1E`, LEFT/RIGHT, wraps at 5):

| `$FF1E` | layout |
|---|---|
| 0 | A-DASH B-SHOOT C-JUMP (default) |
| 1 | A-SHOOT B-JUMP C-DASH |
| 2 | A-JUMP B-SHOOT C-DASH |
| 3 | A-SHOOT B-DASH C-JUMP |
| 4 | A-JUMP B-DASH C-SHOOT |

### 5.5 `play_sound(md, id)` / `stop_sound(md)` (verified)
- **`play_sound`:** writes the ID to Z80 `$1C0A` (first free of `$1C0A-$1C0C`) between frames. This is the
  same thing `$7FED8` does under bus request, so it works in any mode. For $8C it first uploads the ending
  bank to Z80 `$0F00`; for any other ID it restores the SFX bank if the ending bank is loaded.
- **`play_sound_via_game`:** sets `$EE53`/`$EE67`. It only works in modes that call `$7FED8`.
- **`stop_sound`:** sends $F9 (or $F8 with fade) and clears `$EE67`.
- **`driver_state`:** active tracks and the playing music ID, found from the first music track's data
  pointer against the music headers.

Results of `python tools/re/qs_game_sound.py` (from Duckburg gameplay, tick enabled):
- The level's own request plays $83, and `$EE53`=$92 plays $92.
- All 24 music IDs $81-$98: OK. Each is identified from the track pointers ($8C through the ending bank).
- 72 of 81 SFX/special IDs start a track: all of $A0-$E6 plus $F0. $E7-$EF don't (empty entries).
- $F9 stops all 8 music tracks at once.
- $F8 starts the fade (counter 80, down to 50 after 120 frames; DAC and PSG off at once).

---------------------------------------------------------------------------------------------------------

## 6. Other PC-port notes
- **Controller:** `$2102` (in VBlank) reads 3-button pad 1.
  - `$8FB0` = held, `$8FB1` = newly pressed. Raw, active-high: bit 7 START, 6 A, 5 C, 4 B, 3 RIGHT,
    2 LEFT, 1 DOWN, 0 UP. Menus use these.
  - `$4F70` remaps them through `$FF1E` into the layout-0 order: `$8F2C` = held, `$8F2D` = pressed
    (A = dash, B = shoot, C = jump). Gameplay uses these.
  - Cut-scenes (`$EE5E`) replace `$8F2C` with scripted input `$8FA0`.
  - Demo playback (`$EE50`) replaces `$8FB0`/`$8F2C` from `$EF40`.
- **Demo mode:** the title timeout sets `$8F7C`=5. Mode 6 decrements it and plays demo index `$8F7C`
  (4 down to 0); when it goes below 0, the game returns to mode 0. Order:
  - Duckburg (loc 1, entry 0)
  - Mexico (2, 1)
  - Transylvania (3, 3, bubble gum, red plunger)
  - Viking ship (4, 1)
  - Egypt (7, 3, red plunger)

  Tables: `$49DE` plunger (bytes), `$49E4` location, `$49EE` entry, `$49F8` weapon (words). Input comes
  from trap4 $38 (ROM `$7E966`, Kosinski, 2854 bytes, unpacked to `$B000`: 5 word offsets, then 4-byte
  records (pad word, frames), ending with $FFFF). `$2174` copies the pad word to `$8FB0` and `$8F2C`; a
  real START press ORs in $8080, which ends the demo and goes to mode 0. Demo lives 3, power 5.
  Observed: demo 1 starts at frame 3088 (loc 1/0), demo 2 at 5890 (loc 2/1), demo 3 at 8857 (loc 3/3,
  weapon 2, plunger 1). Music $83, $84, $88.
- **Pause (mode 8, `$07AA`):** entry needs `$8FA6`==0; it is $FF after a map arrival and $3C after a reload.
  START is only read by the normal player states (`sne $EE5A` at `$65B4`, `$66CA`, ...), not during the
  hurt state $1A. In testing, a START press during knock-back was ignored and the next one paused.
  - Cursor `$8F9C`: 0-2 weapons, 3-8 items (`$EEE0-$EEE5`, grid 3x2), 9 CALL THE AIRPLANE (needs `$EE5C`,
    else msg $38), $0A USE, $0B LOOK. `$8F9E` = selected item.
  - USE: key/tear/scepter need their `$EED6-$EED9` place flag (else msg $39). The note shows msg $0A; the
    diary sets `$EEFA` (msg $1B) if `$EEF8`, else msg $14.
  - START resumes (`$D54`). Music keeps playing (5.1).
- **Lag:** if the main loop overruns a frame, VBlank does nothing (no pad read, no DMA).
- **Region:** English text needs `$FF0C` bit 7 set (overseas console). The PAL bit selects 30-row VDP
  values.
- **No save and no password.** The only persistence is `$FCA0` (items at the last map visit), used by
  continues. OPTIONS survive a game over (RAM above `$FCFF` is not cleared) but not a reset.
