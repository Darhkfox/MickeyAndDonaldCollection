# World of Illusion (USA) — game logic reverse engineering

ROM: `World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin` (1 MB, header
"GM MK-1070 -00"). All addresses are 68000 addresses; RAM is `$FFxxxx` (capstone prints absolute-short
RAM as `$xxxx.w`). Z80 addresses are written `Z80 $xxxx`.

Scripts (all runnable, all from a clean power-on unless stated):

| file | purpose |
|---|---|
| `tools/re/woi_game_util.py` | shared helpers: screenshots/grids, RAM diff, better xref search, `to_board()` (power-on → options board) |
| `tools/re/woi_game_levelselect.py` | `start_level(md, index, mode)`, `start_area(md, level, area, mode)`, `start_area_direct(...)`; main = verification grids |
| `tools/re/woi_game_cheats.py` | ROM patches + per-frame RAM cheats; main = verification in 1P and 2P |
| `tools/re/woi_game_sound.py` | `play_sound(md, id)`, `stop_sound(md)`, `fade_out(md)`, `driver_state(md)`; main = full ID sweep + sound-test check |
| `tools/re/woi_game_z80.py` | rebuilds the Z80 driver image exactly as the 68000 uploads it; disassembles it (uses `coi_game_z80dis`) |
| `tools/re/woi_game_levelselect_{1P_MICKEY,1P_DONALD,2P,2P_SWAP}.png` | proof grids: every stage started in every applicable mode, after moving |
| `tools/re/woi_game_cheats.png` | proof grid for the cheats |
| `tools/states/woi_game_board.state` | options card board, ready for input |
| `tools/states/woi_game_l1_{1pm,1pd,2p}.state` | level 1 start, 1P Mickey / 1P Donald / 2P, 30 frames into gameplay |

Note: `tools/states/woi_stage1.state` currently contains the *options board*, not stage 1 (it was
overwritten by someone else at 21:05); use `woi_game_l1_1pm.state` for a stage-1 state.

---------------------------------------------------------------------------------------------------

## 1. Game state machine

### 1.1 Main loop and mode variable
Reset ($206) → hardware init ($300..$3D2: VDP, Z80 stub, pads, `$498` clears `$FFFFD0`/`$FFFFD6`) →
main loop:
```
0003d6: move.w $ffd0.w,d0 ; add.w d0,d0 ; add.w d0,d0
0003de: movea.l $3f2(pc,d0.w),a0
0003e2: jsr (a0)                      ; run the current mode handler (one "tick")
0003e4: jsr $7c76.l                   ; $FFFC60=1, enable IRQ, wait until VBlank clears it
0003f0: bra.b $3d6
```
* **`$FFFFD0` (word) = main mode.** Handlers that need a one-time init do `bset #7,$FFFFD0` (sets
  bit 15 of the word) and branch to their init when the bit was clear, so you observe e.g. `$800D`.
  Mode index = `$FFFFD0 & $7F`. Writers: `move.w #n,$FFFFD0` (list via `python tools/re/woi_game_util.py 0xFFFFD0`).
* `$FFFFD2` (word) = VBlank variant (jump table at `$D96`, 8 entries: which DMA/scroll uploads the
  VBlank handler `$D74` does). All variants end with `$42EC` (pads) and `$F609E` (sound mailbox).
* `$FFFC90` (word) = general countdown timer, decremented every VBlank; `$FFFC92` = frame counter.

Mode table at `$0003F2` (19 longs):

| mode | handler | meaning (verified by screenshots and write-watch traces) | exits to |
|---|---|---|---|
| $00 | $001D6A | SEGA logo (palette cycle from `$A4200`) | $0F |
| $01 | $001F30 | New game / continue init: `$F6000` (reload sound driver), tries `$FFFD52` = 3, or 6 if 2P (`$FFFFD8` bit 1), hearts `$FFA038`=`$FFA0B8`=5, cards `$FFFCC2`=0 | $02 |
| $02 | $001F5E | Area load: clears flags, loads map/tiles/objects of (`$FFFFDA`,`$FFFFDE`), player objects (`$BC7A`, `$BCB0`), HUD (`$1E3DA`), area header (`$AA2A`: positions + music), fades in (`$B36E`) — screen is black while loading, **there is no separate stage-intro/curtain screen** | $03 |
| $03 | $00200A | Gameplay (see flags below) | $02, $0C, $0E, $10, $12, $0D |
| $04 | $001134 | Attract "cast" intro (Goofy, Minnie, Daisy, … curtain, "World of Illusion"); music $82. START skips to title | $05 |
| $05 | $0019FA | Title ("PRESS START BUTTON"). START (`$FFFCB3` bit 7) → $06; timer `$FFFC90` expiry → $09 | $06, $09 |
| $06 | $001A36 | Opening story book (routine `$644`, chapter `$FFA81D`=0, music $83). START skips | $0D |
| $07 | $00113A | Title entry from the demo (START pressed during demo; skips the cast pages, `cmpi.w #7,$FFFFD0` at $1340) | $05 |
| $08 | 0 | unused | |
| $09 | $0021CE | Demo init: `$FFFFD6`=(`$FFFFD6`+1) mod 6 selects demo; saves `$FFFFD8` to `$FFFC8C`; sets `$FFFFD8`/`$FFFFDA`/`$FFFFDE` from tables $2232/$2238/$2244; `$FFFFC0`(RNG)=0; `$FFFD5E`=demo flag; runs mode 1+2 inline | $0A |
| $0A | $002250 | Demo playback. START (`$FFFCB6` bit 7) → $07; end of input stream (`$FFFD5F`) → $00 | $07, $00 |
| $0B | 0 | unused | |
| $0C | $002146 | Pause (see §6.1) | $03 |
| $0D | $020BF0 | Options card board (sub-state `$FFA850`, §2.3) | $01 |
| $0E | $01FB0C | Game over: PASSWORD card (shows the 4-card password, C → $0D) or CONTINUE card (→ `$21ED6` reset area to level start → $01). No continue counter: continues are unlimited | $0D, $01, $00 |
| $0F | $001E18 | "ILLUSION SHOT PICTURES presents" | $04 |
| $10 | $0039DA | 2P "continue alone": LEFT/RIGHT choose Mickey or Donald, START → `$FFFFD8`=1/0, area = `$FFFC86`/`$FFFC87`, tries=1, hearts 5 | $02 |
| $11 | $0222DA | Ending (set by the final-boss object at $1AED2), music $9F/$9B/$9C/$9E; ends with mode 0 ($228DC) | $00 |
| $12 | $01A25C | Between-chapter story book after a boss (+1 heart each, or +1 try if already 8), then next level/area | $02 |

Gameplay (mode 3) exit flags, tested in this order at `$204A`:

| flag | set by | effect |
|---|---|---|
| `$FFFC82` | area exits (`$9B16` family, objects) | area change → mode 2 (fade `$B3BC`, or `$B380` keeping the music when `$FFFC8B` bit 0 says the next area uses the same tune) |
| `$FFFC88` | treasure-chest object after a boss (`$1A254`) | → mode $12 |
| `$FFFC7F` | death with tries left (`$F14E`/`$F1BA`) | restart current area → mode 2 |
| `$FFFC66` | death with no tries left (`$F154`/`$F168`) | → mode $0E (game over) |
| `$FFFC68` | 2P "1 try is left, play in 1 player mode? YES" (`$F2EC`) | → mode $10 |
| START new-press on pad 1 (`$FFFCB7` b7), or pad 2 (`$FFFCB9` b7) in 2P | | SFX $BD, → mode $0C |

Observed attract timeline from power-on without input: mode 0 (f0) → $0F (f286) → 4 (f600) → 5
(f3300) → 9 (f3900) → $0A (demo). Tapping START every 20 frames: 0 → $0F (f287) → 4 (f341) → 5 (f555) →
6 (f621) → $0D (f777, interactive when `$FFA850`==1 ~f900) → 1 → 2 → 3.

### 1.2 Demo
Six demos, index `$FFFFD6+1` (the first after power-on is index 1). Input stream pointer `$FFFD58`
(table of 6-byte records at `$22AC` + offset table: long = pad1 word<<16 | pad2 word, word = duration);
`$4468` replaces the remapped pad words `$FFFCB2`/`$FFFCB4` while `$FFFD5E` is set; end marker
`$FFFFFFFF` sets `$FFFD5F`.

| index | `$FFFFD8` | level | area | content |
|---|---|---|---|---|
| 0 | 3 | 3 | 0 | 2P, under the sea |
| 1 | 3 | 1 | 0 | 2P, forest (first demo after power-on, verified) |
| 2 | 1 | 1 | 2 | 1P Mickey, sunny woods |
| 3 | 3 | 2 | 0 | 2P, rocky mountain |
| 4 | 3 | 1 | 3 | 2P, treehouse |
| 5 | 0 | 3 | 3 | 1P Donald, tropical island |

The demo resets the RNG seed (`clr.l $FFFFC0` at $221A) so demos are deterministic.

---------------------------------------------------------------------------------------------------

## 2. Players, modes and pads

### 2.1 Player mode `$FFFFD8` (byte)
| bit | meaning |
|---|---|
| 1 | 2-player game |
| 0 | 1P: 1 = Mickey, 0 = Donald. 2P: 1 = pad 1 plays Mickey, 0 = pad 1 plays Donald |
| 5 / 6 | 2P runtime: player 2 / player 1 is out (dead and waiting) — set at `$F25C`, cleared by `andi.b #3` at every area load (`$BCB6`) and respawn (`$ECD4`) |

Values: `$01` 1P Mickey (power-on default, `$490`), `$00` 1P Donald, `$03` 2P (pad 1 Mickey),
`$02` 2P (pad 1 Donald). Helper `$9ABC` tests `(FFD8 & 3) == 1` (1P Mickey).

### 2.2 Player objects
The two player objects are $80-byte slots (ordinary objects are $40 bytes in the list at `$9000`):
**`$FFA000` = pad 1's character, `$FFA080` = pad 2's character (2P only)**. `$BCB0` builds them at
area load: `$33(a0)` = character (1 = Mickey, 0 = Donald; so in 2P_SWAP `$FFA033`=0, `$FFA0B3`=1 —
verified). Useful fields (offsets from the object base):

| off | size | meaning |
|---|---|---|
| $00 | w | object type (1 = player) |
| $02 | w | animation/state (bit 15 = changed) — `$EFA0` sets it |
| $10 / $14 | w | x / y (world pixels) |
| $0F | b | hit flags this frame (bit 0 = touched by enemy/hazard; set by collision `$F358`) |
| $33 | b | character: 1 Mickey, 0 Donald (selects voice SFX, graphics) |
| $35 | b | invulnerability timer (bits 0-6 frames, bit 7 = don't blink); counted down by `$EFC4` |
| $37 | b | extra damage of the pending hit (hearts lost = $37+1; $7C = certain death) |
| $38 | b | **hearts** (0..8) |
| $3E | b | bit 0 = invulnerable (damage handler skips hits), bit 7 = other immunity |
| $3F | b | bit 1 = hurt/knock-back state |
| $44 | w | pad word for this player (held<<8 | new-press), filled by `$432E` |
| $6D | b | death: bit 0 dying, bit 1 fell into a deadly tile, bit 7 death animation finished |

Hearts are **per player**; tries are **shared** (§4).

### 2.3 Options card board (mode $0D) and player selection
Sub-state `$FFA850` (jump table `$20C12`): 0 init, 1 card choice (`$20D6E`), 2 PASSWORD (`$20E06`),
3 PLAYER 1P/2P (`$20E6A`), 4 SOUND (`$21004` + card object `$2197E`), 5 CONTROL (`$21042`),
6 PLAYER Mickey/Donald (`$20F66`). Card cursor `$FFA86A` (col,row) → card index `$FFA852`; card →
sub-state table `$20DC8` = `[3,2,0,0,4,5,0,0]`. The cursor is clamped to the 2x2 block
PLAYER/PASSWORD/SOUND/CONTROL (verified: RIGHT/DOWN never reach the three blank cards or the JOKER);
C selects, B backs out, START anywhere leaves the board and starts the game (`$20DDA`: fade,
`$FFFFE8`=1, mode 1).

PLAYER card: choose 1P/2P (`$FFA85A` bit 1), then Mickey/Donald (`$FFA85A` bit 0 = pad 1 is Mickey),
copied to `$FFFFD8`. 2P is refused if port 2 reports "no device": `$22036` reads the peripheral IDs
into `$FFA882`/`$FFA883` (`$0F` = nothing; `$0D` = 3-button pad; our DLL reports `$0D`), and board
init clears `$FFFFD8` bit 1 when `$FFA883`==`$0F`.

The board START handler (state 1) calls `$21FF6` = `FFDA=1, FFDE=0, FFE0=0` (always level 1 unless a
password was entered).

### 2.4 Pads
VBlank calls `$42EC`:
* `$4380` reads the 3-button pads: `$FFFCB6` pad 1 held, `$FFFCB7` pad 1 new-press, `$FFFCB8`/`$FFFCB9`
  pad 2. Bits (active high): 0 U, 1 D, 2 L, 3 R, 4 B, 5 C, 6 A, 7 START.
* `$43B4` applies the CONTROL configuration (`$FFFFD4` pad 1, `$FFFFD5` pad 2, 0..3) and writes the
  logical pads `$FFFCB2/$FFFCB3` (pad 1 held/new) and `$FFFCB4/$FFFCB5` (pad 2). Table `$43EA+4*cfg`
  gives, for logical B/C/A, the raw bit that drives it.
* `$432E` gives each player object its pad word: `$FFA044` ← pad 1, `$FFA0C4` ← pad 2 (zero in 1P);
  `$FFA032` != 0 swaps them; input is masked while `$36(a0)` bit 0/2 (cutscenes, magic).
* Menus/pause read the raw `$FCB6..$FCB9`; gameplay reads the logical words.

Logical buttons in gameplay: **C = jump (SFX $A5), B = cape attack (SFX $A1), A = no action**; any
button casts the magic at a magic spot (§3.4). CONTROL configurations (verified by pressing A/B/C with each
value in `$FFFFD4` and watching the logical word and the SFX):

| cfg | A | B | C |
|---|---|---|---|
| 0 (default) | – | attack | jump |
| 1 | attack | jump | – |
| 2 | – | jump | attack |
| 3 | jump | attack | – |

### 2.5 What differs in 2P
* Two player objects, hearts separate (`$FFA038`, `$FFA0B8`), each starts with 5, max 8.
* One **shared tries counter** `$FFFD52`, 6 at a new 2P game (3 in 1P).
* Routes: level areas differ for 1P Mickey, 1P Donald and 2P (§3.2); the 2P routes contain
  co-operative obstacles (e.g. treehouse, door hall).
* Death in 2P (`$F15A`): if tries remain the dead player respawns (in place with 5 hearts and 120
  frames of invulnerability, or at a respawn point from the area table `$FFFD94`; if both are down the
  area restarts). When a death leaves exactly 1 try, the game asks "1 TRY IS LEFT. DO YOU WISH TO PLAY
  IN 1 PLAYER MODE? YES/NO" (object $24 at `$FFA640`, result `$FFFC67`): NO continues 2P, YES → mode
  $10 to pick the character, whose 1P restart area comes from the current area header (`$FFFC86`
  Mickey / `$FFFC87` Donald). Verified with tries=2 + a lethal hit.
* Either pad pauses/unpauses (§6.1). The camera follows the average of both x positions
  (`$7D78`), or the survivor when one is out.
* Voices differ per character (`$BDEA`: Mickey uses id, Donald id+1).

---------------------------------------------------------------------------------------------------

## 3. Levels

### 3.1 Level variables
* **`$FFFFDA` (word): level 1..5.** (Level 0 exists in the tables as a copy of level 1.)
* **`$FFFFDE` (word): area** = entry point inside the level (index into the area-header table).
* `$FFFFDC` (word): room/map index, copied from the area header by `$AA2A` (several areas can share a
  room with different start positions).
* `$FFFFE0` (word): progress value; the password writes `$FFFFE1` = `[0,6,$0C,$16,$1D][level-1]`
  (table `$21E7E`; for level 4 the code reuses d0 = player-mode bits by mistake). No reader was
  found (abs.w xref search; starting level 2 with 0/6/$1234 gives identical code coverage and RAM
  after 300 frames), so it can be written with the password value or left alone.

Area-header table: `$0E2548` → word offset → per-level offset table (6 entries, level 0 = level 1) →
per-area offsets → 18-byte header, applied by `$AA2A` at area load:

| off | size | meaning |
|---|---|---|
| +0 | w | room index → `$FFFFDC` (selects map, tiles, room init `$9E7E` table and exit handler `$FFFD78`) |
| +2,+4 | w,w | plane A start scroll y, x (`$8538`, `$8510`) |
| +6,+8 | w,w | plane B start scroll y, x |
| +A,+C | w,w | player start y, x (2P: P1 x-16, P2 x+16) |
| +E | b | `$FFFC70` (start facing/animation offset) |
| +F | b | `$FFFC86` = area where 1P **Mickey** restarts if a 2P game drops to 1P here |
| +10 | b | `$FFFC87` = same for **Donald** |
| +11 | b | music id; requested (`$FFFD2C`) only if different from `$FFFD2B` |

Area counts: L1 6, L2 19, L3 11, L4 20, L5 30.

Area exits: every room has an exit handler (`$FFFD78`, set by the room init at `$9E7E`) that calls
`$9B16` with a list of rectangles (`$E3E06…`: word count-1, then `x1,x2,y1,y2,param`); in 2P both
players must be inside the rectangle and alive. The action (d6, table `$9B9A`) is: 0 go to area
`param`; 1 L5 door puzzle; 2 back to the L5 door hall; 3 next level; 4/5 `param` = hi byte for 1P, lo
byte for 2P ($FF = next level); 6 L5 route split ($D 2P / $B 1P Mickey / 1 1P Donald); 7 area + VDP
reg tweak; 8 next level (area 1 for 1P Donald else 0). Objects also change areas (warps, trap doors).
Level-to-level: boss killed → treasure chest object (music $99) → `$FFFC88` → mode $12 story book →
table `$1A354[level]` = (level, area): L1→(2,0), L2→(2,$12), L3→(3,9), L4→(4,$11) (post-boss areas that
lead to the next level); the final boss starts the ending (mode $11).

### 3.2 Stage list and routes (per player mode)
Level names are descriptive (the game shows no level titles). Story-book chapter after each boss
(`$FFA81D` = level + 0 for 1P Mickey, +4 for 1P Donald, +8 for 2P) teaches the magic of the next
level: carpet (L2), bubble (L3), "transported to another box" (L4), "command cards" (L5).
M = 1P Mickey, D = 1P Donald, 2 = 2P. Areas and exits are from the code (exit tables quoted), the
pictures are in the proof grids.

**Level 1 — Enchanted forest** (music $84/$85, boss $97)
| area | room | description | who | exits (code) |
|---|---|---|---|---|
| 0 | 0 | forest start | M D 2 | 1P → 2, 2P → 3 (x3056-3071,y864-880) |
| 1 | 1 | beanstalk | D | → 4 |
| 2 | 2 | sunny woods | M (D passes its start) | Donald → 1 (x256-512,y≤352); others → 4 |
| 3 | 3 | treehouse scaffolding | 2 | → 4 |
| 4 | 4 | spider cave | M D 2 | → 5 |
| 5 | 5 | boss: spider-web cave | M D 2 | boss → story → L2 area 0 |

Routes: M 0→2→4→5, D 0→2→1→4→5, 2P 0→3→4→5.

**Level 2 — Rocky mountain & sky (magic carpet)** (music $86 $88 $87 $89, boss $97)
| area | room | description | who | exits |
|---|---|---|---|---|
| 0 | 0 | cliff start, carpet magic spot | M D 2 | M → 12 (x≥5072,y<110); D → 1, 2P → 15 (action 4, param $010F) |
| 1–11 | 1–11 | underground river on a raft (11 short rooms, left/right exits chain 1→2→…→11) | D | 11 → 16 |
| 12 | 12 | storm mountain | M | → 13, or → 16 (x≥1560,y≥390) |
| 13, 14 | 13, 14 | storm mountain 2/3 | M | 13 → 14 → 16 |
| 15 | 15 | cave | 2 | → 16 |
| 16 | 16 | above the clouds | M D 2 | → 17 |
| 17 | 17 | boss: stone-door cave | M D 2 | story → 18 |
| 18 | 18 | after the boss: bubble magic spot (press a button: "Alakazam", water rises) | M D 2 | → L3 area 0 (verified) |

**Level 3 — Under the sea (magic bubble)** (music $8A $8B $8C, boss $97)
| area | room | description | who | exits |
|---|---|---|---|---|
| 0 | 0 | open sea start | M D 2 | → 10 (action 7) |
| 10 | 10 | open sea 1b | M D 2 | → 1 |
| 1 | 1 | coral temple | M D 2 | D (handler `$8B1A`) → 2 (left edge); M/2P (handler `$8B42`, action 5) → 4 (1P) / 5 (2P) |
| 2 | 2 | passage | D | → 3 |
| 3 | 3 | tropical island surface | D | → 6 |
| 4 | 4 | pink coral reef | M | → 6 |
| 5 | 5 | ice-block ruins | 2 | → 6 |
| 6 | 6 | open sea 2 | M D 2 | → 7 |
| 7 | 7 | sunken ship | M D 2 | → 8 |
| 8 | 8 | boss inside the ship | M D 2 | story → 9 |
| 9 | 9 | after the boss | M D 2 | swim to the top → L4 area 0 (M, 2P) / 1 (D) (verified) |

**Level 4 — Giant house & sweets (box teleport)** (music $8D $90 $8F $8E, boss $97). A hub of rooms
0/1/4/5/7/11 (handler `$8D9C`) with two teleport boxes (rooms 2/3 = areas 2/3); `$FFFC89` remembers
hub progress (bit 0 / bit 1 set by the room inits).
| area | room | description | who | exits |
|---|---|---|---|---|
| 0 | 0 | house hub start | M 2 | hub table M `$E3FD8`: → 15, box → 2 / 3; 2P `$E3FF8`: → 14, → 2 / 3, → 12 |
| 1 | 1 | house hub start | D | hub table D `$E4022`: → 14, → 2 / 3, → 8 (`$E404C` without the → 8 exit once `$FFFC89` bit 0 is set) |
| 2, 3 | 2, 3 | teleport boxes | all | → 4/5 (M, 2P), 6/7 (D), 18/19 (D after the storybook) |
| 4–7, 9, 13, 18, 19 | 4, 5, 7, 11 | other hub positions | all | hub tables |
| 8 | 6 | storybook forest | D | → 9 |
| 10 | 8 | flying over the sky | M (Donald's animation table has no flying entry: $36/$37 = 0 at $03FAA4, so he is drawn as garbage - the port's level select offers it to Mickey only) | → 11 |
| 11, 14 | 12 | candy land | all (14 = D/2P entry) | → 10 (top right) or → 16 |
| 12 | 10 | toy / jewellery box | 2 | → 13 |
| 15 | 9 | candy land | M entry | → 10 or → 16 |
| 16 | 13 | boss: candy land witch | all | story → 17 |
| 17 | 14 | after the boss: kitchen | all | walk left → L5 area 0 (verified) |

**Level 5 — The magician's garden & castle (magic cards)** (music $94 $96 $95 $9D, final $98)
| area | room | description | who | exits |
|---|---|---|---|---|
| 0 | 0 | magic garden start | M D 2 | action 6: D → 1, M → 11, 2P → 13 |
| 1–10 | 3, 4, 1 | hedge garden (warp objects `$118A8`, `$D1DE`) | D | room 1 → 23 |
| 11, 12 | 5, 6 | palace hall | M | 11 → 12 → 23 |
| 13 | 7 | door hall, 5 doors (action 1, state at `$FF7F00`) | 2 | each door → one mini stage 14–17 (rooms 8–11 re-use the forest cave $85, sky $86, sea $8A, toy box $8E); return to the hall at 18–22; after the doors → 23 |
| 23, 24 | 13, 14 | starry stage (constellations) | M D 2 | 23 → 24 → 25 |
| 25 | 15 | final boss (the magician) | M D 2 | → ending (mode $11) |
| 26–29 | 16, 0 | trap doors in the garden (`$D036`: areas $1A-$1C), falling through the forest | M D 2 | 26 → 29 |

The unreferenced table at **`$021E8A`** (37 `(level, area)` byte pairs, nothing points to it) is the
developers' list of section starts — it is used as the level-select stage list (`STAGES` in
`woi_game_levelselect.py`, index 0..36). A second unreferenced-looking structure at `$21D4A`
(per level: area + `$60xx` tile words) looks like the remains of a debug stage menu; only its first
word is read (by the password code). No built-in level select or cheat code is reachable: the board
cursor cannot reach the blank cards/JOKER, and no code compares button sequences (all readers of
`$FFFCB2..$FFFCB9` checked).

### 3.3 Passwords
Game over → PASSWORD shows 4 cards; the board's PASSWORD card enters them (cursor LEFT/RIGHT, C
cycles the card value 0→5→0, START validates, B cancels). RAM: `$FFA854..$FFA857` = values 0..5.
Faces: **0 Daisy (Q♥), 1 Minnie (Q♠), 2 Goofy (K♥), 3 Pete (K♣), 4 Scrooge (K♦), 5 Pluto (J♠)**.

Validator `$21DF6`: the long at `$FFA854` is searched in the 20-entry table `$0209A0`; index i →
`$FFFFD8` low bits = `[1,0,3,2][i/5]`, level = `i%5+1`, area 0 (1 for 1P Donald level 4),
`$FFFFE1` progress. Invalid → `$FFA858` bit 0 (error), board stays. Complete list (cards in order):

| level | 1P Mickey | 1P Donald | 2P (pad 1 Mickey) | 2P (pad 1 Donald) |
|---|---|---|---|---|
| 1 | 1 2 0 5 Minnie Goofy Daisy Pluto | 2 1 0 3 Goofy Minnie Daisy Pete | 2 4 5 3 Goofy Scrooge Pluto Pete | 4 3 1 2 Scrooge Pete Minnie Goofy |
| 2 | 3 0 2 4 Pete Daisy Goofy Scrooge | 5 4 3 1 Pluto Scrooge Pete Minnie | 5 2 1 3 Pluto Goofy Minnie Pete | 1 2 5 4 Minnie Goofy Pluto Scrooge |
| 3 | 2 5 4 1 Goofy Pluto Scrooge Minnie | 3 2 0 5 Pete Goofy Daisy Pluto | 4 1 2 0 Scrooge Minnie Goofy Daisy | 5 0 1 4 Pluto Daisy Minnie Scrooge |
| 4 | 0 4 5 3 Daisy Scrooge Pluto Pete | 0 4 2 5 Daisy Scrooge Goofy Pluto | 1 0 3 5 Minnie Daisy Pete Pluto | 3 1 4 0 Pete Minnie Scrooge Daisy |
| 5 | 4 3 2 5 Scrooge Pete Goofy Pluto | 5 2 3 4 Pluto Goofy Pete Scrooge | 3 4 5 2 Pete Scrooge Pluto Goofy | 5 3 4 0 Pluto Pete Scrooge Daisy |

Verified: game over at level 1 shows Minnie-Goofy-Daisy-Pluto (1 2 0 5); entering entries 1 and 7
through RAM + START starts level 2 as 1P Mickey and level 3 as 1P Donald; every password is exercised
by the level select below (all 4 modes × 5 levels start correctly). Passwords only encode
(mode, level) — no hearts, tries, or area.

### 3.4 Verified level-select technique
Implemented in `tools/re/woi_game_levelselect.py` (`start_area`, `start_level`), all from power-on:
1. Tap START every 20 frames until `$FFFFD0` == `$800D` (SEGA → Illusion Shot → cast → title → story
   book → options board, ≈ frame 780), then wait until `$FFA850` == 1 plus 30 frames (the board ignores
   START until its intro animation is done).
2. RIGHT (3 frames, +20), C (3 frames, +40) → PASSWORD card, `$FFA850` == 2.
3. Write the game's password for (mode, level) into `$FFA854..$FFA857` (the card pictures are not
   redrawn; only RAM matters).
4. Hold START: on the frame the validator runs `$FFFFDA` becomes the level and `$FFFFD8` the mode.
5. **For an area other than the level start, write `$FFFFDE` = area now** — the board is still fading
   out for ~30 frames; mode 1 (`$1F30`) and mode 2 run afterwards and never touch `$FFFFDE`.
6. Mode 1 sets tries (3/6), hearts (5/5), cards (0) exactly like a new game; mode 2 loads the area
   with its music (header +$11), palette, HUD, and fades in; gameplay = mode 3 (~frame 1150).

Equivalent without the password card (`start_area_direct`): press START on the board; on the first
frame where `$FFFFD0` reads `$0001` (mode 1 has not run yet) write `$FFFFD8` (low 2 bits),
`$FFFFDA`, `$FFFFDE`, `$FFFFE0`. RAM diff against the password path at gameplay start: player
objects `$FFA000-$FFA0FF` identical, globals `$FFFC00-$FFFFFF` differ only in timers/stack and
`$FFFFE1` (see 3.1). **For a native port the simplest hook is the entry of mode 1 (`$1F30`): set
`$FFFFD8/$FFFFDA/$FFFFDE` (and optionally `$FFFFE0`) before it runs.**

Abilities: there are **no ability flags to set**. The magic of each level is scripted per room: the
room init enables the magic button (`$FFFC74`=1, cleared at every area load by `$BCEE`), `$6206` holds
the players at a barrier x until the spell is cast, casting (any button, player routine `$DCD2`,
voice $D3/$D4 "Alakazam", SFX $E8) sets the spot flag `$FFFC7B` bit 7 (cleared by the room inits). A
level started through the level select therefore has exactly the abilities of a normal play-through
(verified: L2 start shows the carpet hint text, L2 area 18 bubble spell, L3 bubble swimming). What a
play-through carries over that a password/level-select start resets: hearts (+1 per chapter), tries,
and the card counter.

Proof: `python tools/re/woi_game_levelselect.py <outdir>` starts every `STAGES` entry in every mode
that normally visits it (27 per mode, 4 modes, ~12 s per mode), walks right and jumps (both players in
2P), and saves `woi_game_levelselect_<mode>.png`. Result: all 108 starts reach gameplay with the right
HUD (M/D icons, TRIES 03/06) and the players move; the only "STILL" is L5 area 25 where the final-boss
speech locks input (checked separately: players move after the text). 1P Donald loses a try on the L2
raft because the test input steers blindly (normal gameplay).

---------------------------------------------------------------------------------------------------

## 4. Cheats

### 4.1 Variables
| address | size | meaning |
|---|---|---|
| `$FFA038` / `$FFA0B8` | byte | hearts of pad-1 / pad-2 player. New game, respawn, continue: 5. Max 8 (pickup `$773A` caps at 8; full-heart item `$7756` sets 8). Between chapters +1 (8 → +1 try instead, `$1A264`). Death at 0 |
| `$FFFD52` | word | tries, shared by both players; 3 (1P) / 6 (2P); +1 `$F100` (cap 99), -1 `$F0EE` at every death (floored at 0); 1P: game over when a death leaves 0 |
| `$FFFD54` | byte | set ≠0 to redraw the TRIES digits (HUD `$1E37A`) |
| `$FFFCC2` | word | playing cards collected (item type 5, `$780A`); at 52 → +1 try and reset to 0 |
| `$FFFD31` / `$FFFD41` | byte | hearts currently drawn in the HUD (animate towards `$38(a0)`) |
| `$35(a0)` | byte | invulnerability timer (post-hit 120 = `$78`, respawn 120, invincibility-star item `$7774` sets `$14` + sparkles) |
| `$3E(a0)` | byte | bit 0 = invulnerable |
| continues | – | unlimited (no counter; CONTINUE card always offered) |
| score | – | none in this game |

Damage path (`$F000`, run every frame for each player):
```
00f000: move.b $f(a0),d7 ; andi.b #$fc,$f(a0)     ; d7 = hit flags
00f014: tst.b $6d(a0)    ; bne $f040              ; dying: ignore
00f01a: btst #1,$3f(a0)  ; bne $f040              ; already knocked back
00f022: tst.b $3e(a0)    ; bne $f040              ; invulnerable (blinking)   <- 'invincible' patch
00f028: btst #1,$3d(a0)  ; bne $f040
00f030: btst #0,d7       ; bne $f078              ; hit!
00f078: ... d7 = $37(a0)+1 (damage) ; voice $D5/$D6
00f098: sub.b d7,$38(a0) ; bls $f0ae              ; <- 'infinite_health' patch
00f09e: bset #1,$3f(a0)  ; knock-back (invulnerability timer $78 set when it ends, $E708)
00f0ae: clr.b $38(a0) ; bsr $f0ee (lose a try) ; bset #0,$6d(a0) ; death anim $44
00f0c4: move.b #$78,$35(a0) ; bset #0,$3e(a0) ; sub.b d7,$38(a0) ...   ; <- second patch site $F0D0
00f0ee: subq.w #1,$fd52.w ; bpl ; clr ; st $fd54 ; rts                  ; <- 'infinite_lives' patch
```
Deadly tiles (pits etc.) use a separate path (`$C830`: tile attribute bit 3 while falling → hearts 0,
`jsr $F0EE`, fall-death state $4E) that ignores `$3E`.

### 4.2 Implementations (both work for 1P and 2P; `tools/re/woi_game_cheats.py`)
ROM patches (one word each, recommended for a port):

| cheat | address | original | patched | why |
|---|---|---|---|---|
| invincibility (no damage from enemies/hazards) | `$00F026` | `66 18` (bne.b $F040) | `60 18` (bra.b $F040) | every hit takes the "invulnerable" exit; no knock-back, no blinking. Deadly tiles still kill |
| infinite health | `$00F098`, `$00F0D0` | `9F 28 00 38` (sub.b d7,$38(a0)) | `4A 28 00 38` (tst.b $38(a0)) | hits still knock back and give invulnerability, hearts never drop; the following `bls`/`bhi` see hearts>0 |
| infinite lives | `$00F0EE` | `53 78 FD 52` (subq.w #1,$FD52.w) | `4A 78 FD 52` (tst.w $FD52.w) | covers heart deaths and deadly-tile deaths (`$C85C` calls `$F0EE`); `tst` keeps N=0 so the following `bpl` is taken |

Per-frame RAM writes (between frames, any time; the game only touches these at hits/deaths):

| cheat | write | notes |
|---|---|---|
| infinite lives | `$FFFD52` = max(value, N); `$FFFD54` = $FF when changed | death decrements then the game-over check (`$F142`) happens many frames later, so topping up every frame is enough |
| infinite health / max hearts | `$FFA038` = 8 (and `$FFA0B8` = 8 if `$FFFFD8` bit 1) while hearts ≠ 0 and `$6D(a0)` = 0 | a single hit can still kill if it does ≥ 8 damage (`$37`=$7C hazards) |
| invincibility | `$35(a0)` = `$FF`, `$3E(a0)` \|= 1 for each player while `$6D(a0)` = 0 | no blinking (bit 7). Side effect: `$3E` ≠ 0 also disables the 2P player-to-player contact check (`$EACE`) |
| cards | `$FFFCC2` = 51 | next card gives a try |
| all abilities | – | not applicable: magic is enabled per room (§3.4); forcing `$FFFC74`=1 only enables the cast animation, the effect objects exist only in their rooms |

Verification (`python tools/re/woi_game_cheats.py <outdir>`, level 1 area 0, walk right 1250 frames
through the enemies; then 4 lethal hits injected exactly like the collision code would, via `$0F`/`$37`):
```
1P_MICKEY none:                  min hearts [2]    knockback [True]
1P_MICKEY patch invincible:      min hearts [5]    knockback [False]
1P_MICKEY patch infinite_health: min hearts [5]    knockback [True]
1P_MICKEY ram health=8:          min hearts [7]    (restored to 8 next frame)
1P_MICKEY ram invincible:        min hearts [5]    knockback [False]
1P_MICKEY deaths none:           tries 2,1,0 then mode $0E (game over)
1P_MICKEY deaths patch lives:    tries 3,3,3,3 (mode 3)       ram lives: 3,3,3,3
2P none:                         min hearts [2, 2]
2P patch invincible:             min hearts [5, 5] knockback [False, False]
2P patch infinite_health:        min hearts [5, 5] knockback [True, True]
2P ram health=8 / ram invincible: [7, 7] / [5, 5]
2P deaths none:                  tries 5,4,3,2 (both players die alternately, respawn)
2P deaths patch lives:           tries 6,6,6,6               ram lives(>=3): 5,4,3,3
```

---------------------------------------------------------------------------------------------------

## 5. Sound

### 5.1 Driver and request path
* Z80 driver: SMPS-style, **interrupt driven** (IM 1, update at Z80 `$0038` on every VBlank; no
  dependence on YM2612 timers, so it runs normally in the stubbed DLL). Uploaded by `$0F6000`
  (called by modes 0/1, area loads when the music changes, `$3A44`):
  ROM `$0F60E4` ($DD0 bytes) → Z80 `$0000`, ROM `$0F6EB4` ($C88) → Z80 `$0E00`, ROM `$0F606E` (10) →
  Z80 `$1C00`, `$FFFFCE` → Z80 `$1BFF`; then Z80 reset.
* Music data lives in two 32 KB banks: bank 0 = ROM `$0E8000`, bank 1 = ROM `$0F0000` (Z80 bank register
  written with `$1C3D`+1). Each bank starts with the same master table (Z80 `$8000`): +0 priority table,
  +2 special-SFX table (`$0E80`), +4 music table, +6 SFX table (`$0E00`). The per-music bank is table
  Z80 `$08E1` (31 bytes, `$64` = invalid).
* 68000 side:
  * music / command: `move.b #id,$FFFD2C` (the area loader `$AAE2` also stores `$FFFD2B` = current
    music and skips the request when unchanged; `$B3BC` clears `$FFFD2B` before fades so the next
    area restarts its music);
  * SFX: `move.b #id,$FFFD2A ; trap #9` → `$0F6078` puts it into the first free byte of `$FFFD2D..$2F`;
  * VBlank `$0F609E` (Z80 bus held): if `$FFFD2C` ≠ 0 → Z80 `$1C0A` (SFX of that frame are dropped);
    else each queued SFX ≥ $80 → Z80 `$1C0A`, `$1C0B`, `$1C0C` (packed); clears `$FFFD2C..$2F`.
* Z80 side: `$07B0` checks the 3 mailbox bytes, keeps the one whose priority (priority table
  entry id-$81) ≥ the current `$1C18`, moves it to `$1C09`; `$0436` executes `$1C09`: $81-$9F music
  (`$048A`), $A0-$DF SFX (`$0539`), $E0-$E9 special SFX (`$0530`, index id-$E8), $EA-$ED commands (table
  `$0461`), $EE garbage, ≥$EF (and <$80) stop (`$0716`). `$1C09` = $80 when idle.
* Other Z80 variables: `$1C0D` fade counter ($28 after $EA), `$1C10` pause (68k writes 1 = pause →
  driver sets 2 and silences; `$80` = resume), `$1C18` current SFX priority, `$1C3D` music bank,
  tracks: `$1C40` DAC, `$1C70+$30n` FM1-6, `$1D90+$30n` PSG1-3, `$1E20+$30n` 7 SFX tracks, `$1F70`
  special. Track +0 bit 7 = playing, +3/+4 = data pointer (LE).

### 5.2 IDs
**Music** ($81-$9F; $91-$93 invalid — bank $64). Sound test "MUSIC n" (table `$021F3E`, odd bytes) in
parentheses:

| id | test | where it plays (from area headers +$11 and `$FFFD2C` writers) |
|---|---|---|
| $81 | – | never requested; an empty stub (only the DAC track starts and it plays nothing - silent for 60 s), so the port's sound list leaves it out |
| $82 | 1 | attract: cast intro + title screen (`$1400`) |
| $83 | 2 | opening story book (`$1A48`) |
| $84 | 3 | L1 forest (areas 0-2); demo |
| $85 | 4 | L1 treehouse (2P) and spider cave; L5 2P mini stage 1 |
| $86 | 5 | L2 rocky mountain start / above the clouds; L5 2P mini stage 2 |
| $87 | 6 | L2 Mickey route: storm mountain |
| $88 | 7 | L2 Donald route: underground river raft |
| $89 | 8 | L2 2P route: cave |
| $8A | 9 | L3 under the sea (most areas); L5 2P mini stage 3 |
| $8B | 10 | L3 Donald route: tropical island |
| $8C | 11 | L3 sunken ship |
| $8D | 12 | L4 giant house (hub, kitchen) |
| $8E | 13 | L4 toy/jewellery box (2P); L5 2P mini stage 4 |
| $8F | 14 | L4 candy land |
| $90 | 15 | L4 flying over the sky |
| $94 | 16 | L5 magic garden (start, Donald hedge route, trap-door areas) |
| $95 | 17 | L5 2P door hall |
| $96 | 18 | L5 Mickey route: palace hall |
| $97 | 19 | boss battle (levels 1-4) |
| $98 | 20 | final boss |
| $99 | 21 | options card board (`$20D52`); treasure chest after a boss (`$1A1E0`) |
| $9A | 22 | game over screen (`$1FC8A`) |
| $9B | 23 | ending part (`$231F2`) |
| $9C | 24 | ending part (`$22656`) |
| $9D | 25 | L5 starry stage before the final boss |
| $9E | 26 | ending part (`$23208`) |
| $9F | 27 | ending / staff roll (`$223AA`); also played as a jingle when walking into the box at the end of L4 (kitchen, area 17, `$900E` sends it through the SFX queue — verified) |

**SFX** $A0-$DF (64, all valid: each starts an SFX track; priority from the table). Identified by
context / dynamic traces (`woi_game_sound.py` lists call sites):
$A0 item pickup (hearts, 1-up, star, card), $A1 cape attack (B), $A5 jump (C), $AB jump variant,
$A7 fall into a deadly tile, $AD player stunned/bumped, $BD pause/unpause and board select,
$BF enemy noise (object `$1337E`), $C3 options-board card flip, $DD 2P route switch in L2,
voices (`$BDEA`, Mickey = id, Donald = id+1): $D3/$D4 "Alakazam" (magic cast), $D5/$D6 hurt,
$DE/$DF third voice clip. Sound test "SE 1..6" = $D3 $D4 $D5 $D6 $DE $DF (table `$021F74`).
Special SFX: $E8 magic spell sound (valid, table index 0), $E9 (index 1); $E0-$E7 index before the
table (the game uses $E0 once at `$412A`; it starts the special track through an out-of-table pointer).
**Commands**: $EA fade out music, $EB stop everything (driver reset `$0716`), $EC silence PSG,
$ED stop the special-SFX track (used before the chest jingle), $EF-$FF = stop.

### 5.3 Verified play/stop from a running game
`tools/re/woi_game_sound.py`: `play_sound(md, id)` writes `$FFFD2C` (music, commands) or the SFX queue
`$FFFD2D..` (like trap #9); `direct=True` writes Z80 `$1C0A`. `stop_sound(md)` = $EB + clear
`$FFFD2B`; `fade_out(md)` = $EA. `main()` (runs in ~2 s):
* for every id $81-$FF from gameplay (level 1): after 3 frames the driver has consumed the request;
  every music id except $91-$93 has the expected bank in `$1C3D` and its DAC track pointer
  (`$1C43`) equal to the first track pointer of its SMPS header (all 28 "OK"); every SFX id starts
  1-3 SFX tracks; $EA sets the fade counter `$1C0D`=$28 and 300 frames later all tracks are off;
  $EB clears all tracks (`$1C09` 0 → $80) within 2 frames;
* `direct=True` (Z80 `$1C0A`) verified with $98 (bank 0, DAC pointer = header). While the game is
  paused (Z80 `$1C10`=2) the driver accepts the request into `$1C09` but executes nothing until the
  resume value $80 is written (verified: $8A requested during pause starts right after unpausing).
  Music always wins the priority check (music priorities are $80); an SFX is dropped if its priority
  is lower than the SFX playing (`$1C18`);
* the SOUND card was driven through the real UI (DOWN, C to open, A to play, C for the next entry):
  the ids the game sends are `82 83 84 … 90 94 95 … 9F D3 D4 D5 D6 DE DF` = the tables above (match True).

---------------------------------------------------------------------------------------------------

## 6. Other port notes

### 6.1 Pause
Mode 3 → mode $0C on a START new-press from pad 1 (`$FFFCB7`) or, in 2P, pad 2 (`$FFFCB9`); SFX $BD,
`$FFFC90`=10. Mode $0C (`$2146`) writes pause tiles to VRAM, sets Z80 `$1C10`=1 when the 10-frame
timer reaches 1 (so the pause SFX can play; the driver then sets 2 and stops updating), and resumes on
START from either pad (Z80 `$1C10`=$80, SFX $BD, mode 3). No other game logic runs while paused
(mode 3 handler not called).
Verified: in 2P, pad 2 pauses and unpauses, pad 1 pauses and pad 2 unpauses.

### 6.2 Controller configuration
CONTROL card edits `$FFA85B` (pad 1) / `$FFA85C` (pad 2, selected with C when a pad 2 exists), copied
to `$FFFFD4`/`$FFFFD5` (0..3) on START. Effect table in §2.4 (verified).

### 6.3 RNG
`$7C4C`: seed `$FFFFC0` (long). If 0 → `$2A6D365A`. `x = seed*41; r = lo(x)+hi(x); seed = r<<16 |
lo(x)`; returns r (word). Advanced only when called (no per-frame stirring); RAM is cleared at reset
so the sequence is deterministic from power-on; the demo clears the seed.

### 6.4 Misc RAM
| address | meaning |
|---|---|
| `$FFFFCC` | port 1 pad type byte (from `$A10001`), `$FFFFCD`/`$FFFFCE` region flags (overseas / PAL) |
| `$FFFFD6` | demo index |
| `$FFFFE8` | set when the game was started from the board |
| `$FFFD2B`/`$FFFD2C`/`$FFFD2A`/`$FFFD2D..2F` | sound mailboxes (§5) |
| `$FFFC61` | VDP/scroll mode flags per room |
| `$FFFC70` | area header +E |
| `$FFFC74` | magic button enabled in this room |
| `$FFFC7B` | magic-spot flag (bit 7 = spell cast) |
| `$FFFC86`/`$FFFC87` | 1P restart area for Mickey/Donald (area header +F/+10) |
| `$FFFC89` | L4 hub progress bits |
| `$FFFD78` | current room exit handler (word, code address) |
| `$FFFD94` | respawn point table pointer (2P) |
| `$FFA81D` | story book chapter |
