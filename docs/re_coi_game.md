# Castle of Illusion (USA) – game logic, levels, cheats, sound

Reverse-engineering notes for the PC port features (level select, cheats, sound test / music player).
ROM: `Mickey Mouse - Castle of Illusion (U) [!].bin` (512 KB, header checksum `$4270`).
All RAM addresses are 68000 addresses (`$FFxxxx`; the game uses absolute-short `$xxxx.w`).
Z80 addresses are Z80-side (`$0000-$1FFF` RAM, `$4000` YM2612, `$6000` bank, `$8000-$FFFF` bank window).

Everything below was checked in the headless core (`tools/mdt.py`) unless marked *(inferred)*.

Scripts / data produced (all under `tools/re/`, prefix `coi_game`):

| file | purpose |
|---|---|
| `coi_game_util.py` | shared helpers (screenshot grid, better xref search `python coi_game_util.py 0xFFF300`) |
| `coi_game_levelselect.py` | `start_level(md, index)` from power-on, `warp_level(md, index)` in-game, `to_options(md)`; main = all 28 levels + grid `coi_game_levelselect_grid.png` (`--states` also writes `tools/states/coi_game_lvlNN.state`) |
| `coi_game_cheats.py` | RAM map, HUD-aware RAM setters, ROM patch table (`apply_patch`), in-game verification (grid `coi_game_cheats_grid.png`) |
| `coi_game_sound.py` | `play_sound(md,id)`, `stop_sound`, `fade_out`, `set_pause`, ID tables, driver-state dump, verification |
| `coi_game_z80dis.py` | small Z80 disassembler + exact rebuild of the Z80 RAM image (`python coi_game_z80dis.py 0x4db 40`, `... refs 0x1c0c`) |
| `tools/states/coi_game_options.state` | OPTIONS screen (mode `$0A`) reached from power-on |
| `tools/states/coi_game_lvlNN.state` | level NN (0..27) 75 frames after gameplay starts, made by `start_level` (Normal) |

---------------------------------------------------------------------------------------------------

## 0. Quick reference

| address | size | meaning |
|---|---|---|
| `$FFF300` | word | **game mode** = main-loop handler index ×2 (table at `$0003D8`), see §1 |
| `$FFF302` | word | VBlank job selector (which DMA work VBlank does, 0..6) |
| `$FFF304` | word | general countdown timer, −1 per VBlank while ≠0 |
| `$FFF306` | word | frame counter, +1 per (non-lag) VBlank |
| `$FFF370` | word | lag-frame counter (VBlank while main loop still busy) |
| `$FFF400` | byte | main loop → VBlank handshake (1 = frame done, waiting for VBlank) |
| `$FFF308` | byte | pad 1 **held**, active-high `S A C B R L D U` (bit7..0) |
| `$FFF309` | byte | pad 1 **newly pressed** this frame (same bits) |
| `$FFF310/311` | byte,byte | Mickey's logical input (held/pressed) after control-config remap and reversed-controls |
| `$FFF41D` | byte | **pause** flag (bit0 toggled by START in gameplay) |
| `$FFFF42` | word | **level index** 0..27 (§2) |
| `$FFFF44` | word | round 0..5 (derived from level by `$9FF0`) |
| `$FFFF46` | word | act/sub-area within round (derived) |
| `$FFFF0F` / `$FFFF0D` | byte | difficulty master / working copy: 0 NORMAL, 1 HARD, 2 PRACTICE (OPTIONS edits `$FF0D`, START copies it to `$FF0F`; every level init copies `$FF0F`→`$FF0D`, demo clears `$FF0D`; game code tests `$FF0D`) – a port changing difficulty should write both |
| `$FFFF0E` | byte | control config 0..2 |
| `$FFF41F` | byte | sound-test number −1 (0..24) |
| `$FFF22C` | long | **score**, plain binary (not BCD) |
| `$FFF282` | long | next extra-try score (40000, then +50000) |
| `$FFFF04` | long | high score (50000 at power-on) |
| `$FFF320` | word | **ITEMS** (apples/marbles/...), 0..30 |
| `$FFF322` | word | **POWER** (health stars), 0..5 |
| `$FFF324` | word | **TRIES**, 0..10 (value shown on HUD; 0 → game over after the current death) |
| `$FFFF40` | byte | **continues** left (Normal 3 / Hard 1 / Practice 5) |
| `$FFF446` | byte | post-hit invulnerability / blink timer (`$5B` after a hit) |
| `$FFF40E` | byte | last sound ID requested by the 68000 |
| `$FFF40A` | byte | demo (attract) mode flag |
| `$FFFF00` | long | RNG seed |
| `$FFDC00` | $40 | Mickey's object slot: `+$10` X, `+$14` Y (words, pixel; `+$12/$16` fractions), `+$18/$1C` speeds (long), `+$31` flags (bit1 hit, bit2 dead) |
| Z80 `$1C0C` | byte | direct **music** request slot |
| Z80 `$1C0A` / `$1C0B` | byte | **SFX / command** queue slots |

---------------------------------------------------------------------------------------------------

## 1. Game state machine

### 1.1 Frame loop

Reset `$000200` → hardware init → `$000300`: region byte (`$A10001` → `$FFFF0C`, VDP reg-1 values
`$FFFF08`/`$FFFF0A` = display off/on), cold-boot ROM checksum (sum of words `$200..$7FFFF` must equal
header word `$18E`, else `$4CE` = red screen, dead loop), clears RAM (`$FFFF00-$FFFFFF`, then
`$FF0000-$FFFAFF`), high score `$FFFF04 = 50000`, copies a jump table to `$FFFF60` (`$438`),
VDP init `$AC50`, **Z80 sound driver upload `$79EE0`**, then the main loop:

```
0003C2  moveq   #0,d0
0003C4  move.w  $F300.w,d0          ; game mode
0003C8  move.w  $3D8(pc,d0.w),d0    ; handler table (word addresses)
0003CC  movea.l d0,a0
0003CE  jsr     (a0)                ; run one frame of the current mode
0003D0  jsr     $95B8               ; $F400=1, SR=$2300, spin until VBlank clears $F400
0003D6  bra.b   $3C2
```

VBlank (level 6 vector → `$00CF92`): if `$F400==0` (main loop not finished = lag frame) only
`$F370++`. Otherwise: clear `$F400`; write VSRAM from `$FA80`/`$FAC0` and H-scroll from
`-$FA84`/`-$FAC4` (camera); palette DMA when `$F403` bit0; job table by `$F302`
(sprites/scroll/plane DMAs); **`$D506` RNG step**; **`$AE4E` pad read**; `$F304--` (if ≠0);
`$F306++`. In mode `$10` it also enables H-interrupts (`$D4C6` = per-line VSRAM wave).

Mode handlers run once per frame; long screen builds (`$9D84`, mode inits) call `$95B8` themselves,
so the pad can change without the handler seeing the edge (see the retry logic in
`press_until()` of the level-select script).

### 1.2 Mode table (`$FFF300`, table at `$0003D8`)

| mode | handler | what | exits |
|---|---|---|---|
| `$00` | `$9C94` → `$7A43A` | power-on / restart: request `$D1` (stop), clear `$FF0000-$FFFAFF` (`$7A494`), build SEGA logo | → `$02` |
| `$02` | `$7A4AA` | SEGA logo (START skips) | → `$04` |
| `$04` | `$9CAC` | opening-story init ("ONCE UPON A MOUSE..."), music `$81` | → `$06` |
| `$06` | `$9CDE` | opening story text (script `$6877C`, step `$F40C`, 31 steps) followed by the **title screen**. START while story runs (`$F40C<$1F`) → `$12`; START on title → `$08`; title idle (`$F40C>=$1F` and `$F304<100`) → `$F40A=1`, `$1C` (demo) | `$12` / `$08` / `$1C` |
| `$08` | `$9E96` | OPTIONS init (`$FF0D = $FF0F`) | → `$0A` |
| `$0A` | `$9F38` | **OPTIONS** (DIFFICULTY / SOUND TEST / CONTROL). START → `$9FCC`: `$9FF0` (round/act from level), `$FF0F = $FF0D`, `$A094` (continues), mode `$0C`, or `$28` if level==0 && round==0 | `$0C` / `$28` |
| `$0C` | `$95D2` | **level init**: art, palette, map, objects, HUD (`$99B8`), power/tries (`$96CC`), demo setup (`$98F2`), `$9FF0`, **level music** (`$9686`) | → `$0E` |
| `$0E` | `$97BA` | **gameplay** (details below) | `$0C`,`$10`,`$18`,`$24`,`$2C`,`$00` |
| `$10` | `$FC50` | "warp" wave effect (H-int raster wave) after the warp item (`$F442`) | → `$0E` |
| `$12` | `$9D56` | title shown after skipping the story; 360 frames (`$F348`) or START | → `$1C` |
| `$14` | `$A0B8` | ending text/credits init (script `$68692`, 39 steps) | → `$16` |
| `$16` | `$A0FE` | ending: story + staff credits + "The End" (music `$90`) | → `$00` (clears `$FFFF40..`) |
| `$18` | `$7A530` | **GAME OVER** init: `$FF40--` (continues), `$FF41 = ($FF40≠0)`, music `$9D` | → `$1A` |
| `$1A` | `$7A5E2` | game-over screen: hold LEFT = CONTINUE (→ `$0C`, same level, score 0, tries 3), RIGHT or ~1070 frames = END (→ `$00`) | `$0C` / `$00` |
| `$1C` | `$7A93E` | if `$F40A==0` → `$08`; else copyright screen ("© 1990 THE WALT DISNEY COMPANY...") | → `$1E` / `$08` |
| `$1E` | `$7A9A6` | copyright wait `$78` frames (any button → `$04`) | → `$0C` (demo) |
| `$20` | `$7AB7C` | **castle hallway** init (variant by round `$FF44`, Practice uses a second table), music `$9E` unless already playing (`$F426`) | → `$22` |
| `$22` | `$7ADDA` | hallway cut-scene "WHAT KIND OF ROOM IS BEHIND THIS DOOR?" – Mickey walks to the round's door (~800-1750 frames, not skippable) | → `$0C` (`$7B95A`) |
| `$24` | `$7A9EE` | **round clear / gem tally** init (music `$89`) | → `$26` |
| `$26` | `$7AADE` | tally, score bonuses, extra tries; then `$F414=1`, `$FF42++`, `$FF44++`, items=0 (Practice remaps level) | → `$20` |
| `$28` | `$A170` | castle-entrance story init (old man: "HURRY! BEFORE MIZRABEL..." script `$68668`), music `$9E`, `$F426=1` | → `$2A` |
| `$2A` | `$A1C8` | castle-entrance story; START or end → `$20` | → `$20` |
| `$2C` | `$A110` | ending cut-scene init – set by the Mizrabel object (`$10ECA`) | → `$2E` |
| `$2E` | `$A158` | ending cut-scene (Mizrabel's defeat, flight home) | → `$14` (via `$F418`) |

Text scripts: `$A254(d0)` selects entry `d0` of the table at `$68620` (6 bytes: step count, next
mode, script pointer); `$A20A` advances `$F40C` and switches to the stored next mode `$F418`.

Gameplay handler `$97BA`, per frame:
1. `$F340<0` (fade-out running) and `$F424==0` → request `$D0` (fade music).
2. `$7C2A2` palette fade, `$9954` demo input playback.
3. START edge → `bchg #0,$F41D` + `$9770` (driver pause handshake, §4.5).
4. `$187C` object/sprite system; if paused stop here.
5. `$F0C0`, `$F512`, `$6D52A` (camera/map/object spawn), then exits:
   * `$98DA`: `$F422` (demo finished) → mode `$00`.
   * `$9834`: `$F40F` (Mickey's death sequence finished, set at `$B1BC`) → if `$F449` (tries were
     0) → high score update, **score cleared**, items cleared, mode `$18`; else mode `$0C`
     (restart the level).
   * `$981C`: `$F44F` (boss beaten, set at `$0F48`) → mode `$24`.
   * `$9878`: `$F412` (act exit touched, `$06F0`) → next level (§2.3), mode `$0C`.
   * All exits wait for `$F340==0` (fade finished).
6. `$9C42` score → HUD / extra try, `$9B4A` HUD tile writer.

A warp item (`$076A`) sets `$F442` (→ mode `$10`) and toggles `$F441` bit0; `$F441` bit0 swaps
UP/DOWN, bit1 swaps LEFT/RIGHT in `$CE3C` (Toyland's upside-down section).

There are **no per-act title cards**. The only "intro card" is the castle hallway (modes
`$20/$22`) that precedes the first level of every round, plus the old-man story before round 1.

Verified by logging `$F300` from power-on (attract loop: `0,2,4,6,$1C,$1E,$0C,$0E,0,...`), new game
(`$28,$2A,$20,$22,$0C,$0E`), forced exits (`$0C`, `$24,$26,$20,$22,$0C`), game over (`$18,$1A`,
LEFT → `$0C`, RIGHT → `$00`), forced `$2C` from the last level (`$2C,$2E,$14,$16`, credits, "The End").

---------------------------------------------------------------------------------------------------

## 2. Levels

### 2.1 Variables
* `$FFFF42` (word) **level index 0..27** – the only variable that selects the level. Survives mode
  `$00` (lives in the persistent block `$FFFB00+`).
* `$FFFF44` round 0..5 and `$FFFF46` act are derived by `$9FF0` from tables `$A008` / `$A024`
  (called by the OPTIONS START handler and by the level init).
* Debug label table `$A040` (3 tile codes per level, font: `$10`='0', `$1A`='A'...) – these are the
  developer names shown by the hidden level select.
* Items icon type `$F350` from table `$9B14` (0 apple, 1 marble, 2 clock-tower item).

### 2.2 Level list (play order)

Names are not stored in the ROM (no round names in any text); round names are the ones commonly used
for this game, sub-area descriptions are from screenshots (`coi_game_levelselect_grid.png`).

| play order | index | label | music | description |
|---|---|---|---|---|
| story | – | – | `$9E` | old man at the castle gate (mode `$28`), hallway (`$20`) |
| 1 | `$00` | 1-1-0 | `$82` | The Enchanted Forest – Act 1, forest floor (start of game) |
| 2 | `$02` | 1-3-0 | `$82` | The Enchanted Forest – Act 1, walking trees (music continues from 1-1-0) |
| 3 | `$03` | 1-4-0 | `$83` | The Enchanted Forest – Act 1, spider-web cavern |
| 4 | `$01` | 1-2-0 | `$84` | The Enchanted Forest – Act 2, tree tops / apple trees |
| 5 | `$04` | 1-B-0 | `$9F` | The Enchanted Forest – Boss (evil tree) → gem tally |
| 6 | `$05` | 2-1-0 | `$87` | Toyland – Act 1 (after hallway) |
| 7 | `$06` | 2-2-0 | `$88` | Toyland – Act 2 (upside-down warp) |
| 8 | `$07` | 2-3-0 | `$88` | Toyland – Act 3 (toy soldiers, block castle) |
| 9 | `$08` | 2-B-0 | `$9F` | Toyland – Boss |
| 10 | `$09` | 3-1-0 | `$8B` | The Storm – Act 1 (storm cliffs, cave with waterfall) |
| 11 | `$0A` | 3-2-0 | `$8C` | The Storm – Act 2 (ruins) |
| 12 | `$0B` | 3-3-0 | `$8C` | The Storm – Act 3 (ruins) |
| 13 | `$0C` | 3-B-0 | `$9F` | The Storm – Boss |
| 14 | `$0D` | 4-1-1 | `$8E` | Library/Dessert – Act 1, shelves part 1 (tiny Mickey among jars/books; tea-cup doors) |
| 15 | `$15` | 4-2-0 | `$91` | Library/Dessert – Act 2, dessert land (cakes) (normal exit of 4-1-1) |
| 16 | `$0E` | 4-1-2 | `$8E` | Library/Dessert – shelves part 2 (milk bottle) |
| 17 | `$16` | 4-B-0 | `$9F` | Library/Dessert – Boss (in dessert land) |
| alt | `$12/$13/$14` | 4-T-1/2/3 | `$83` | inside tea cup 1/2/3 (swimming in tea) – reached through door exits of 4-1-1 |
| alt | `$0F/$10/$11` | 4-1-3/4/5 | `$8E` | shelves after tea cup 1/2/3 → boss `$16` |
| 18 | `$17` | 5-1-0 | `$96` | The Clock Tower – Act 1 (castle hall) (after hallway) |
| 19 | `$18` | 5-S-0 | `$96` | The Clock Tower – shaft / stairs |
| 20 | `$19` | 5-2-0 | `$98` | The Clock Tower – Act 2 (gears) |
| 21 | `$1A` | 5-B-0 | `$9F` | The Clock Tower – Boss (jester) |
| 22 | `$1B` | L-B-0 | `$9A` | Castle of Illusion – final battle with Mizrabel (after hallway) → ending |

Round-first levels (entered through the hallway): `$00, $05, $09, $0D, $17, $1B`.
Boss levels: `$04, $08, $0C, $16, $1A, $1B`.

### 2.3 Transitions (verified by forcing `$F412`/`$F44F` in every level)
* Act exit (`$F412`): `$FF42 = table[$98BE + $FF42]`
  `02 04 03 01 05 06 07 08 09 0A 0B 0C 0D 15 16 16 16 16 0F 10 11 0E 17 18 19 1A 1B 1C`.
  Door exits in round 4: the exit object (`$06D6`) with parameter `$22 = n ≠ 0` stores
  `$F453 = n`; then `$FF42 = $11 + n` (n=4 → 3), i.e. tea cup 4-T-n, and sets `$FF4B` (door 1) or
  `$FF4C` (others) – persistent "door opened" flags that make object `$17BA` show the door open.
  Another object (`$0F76/$0F90`, `$F455`) makes the tally path go `$FF42+1` (`$15`→`$0E`).
* Boss beaten (`$F44F`, object `$0F48`) → tally `$24/$26` → `$FF42++`, `$FF44++`, items cleared,
  `$F414=1` (power refill) → hallway `$20/$22` → `$0C`.
* Mizrabel beaten → object `$10EA6` sets mode `$2C` (from code; the ending itself was verified by
  forcing `$2C` in L-B-0: `$2C,$2E,$14,$16`, credits, "The End", mode `$00`).
* PRACTICE: the tally sets the low byte of `$FF42` from `$7A93A` (`07 09 00 00`): after the forest
  boss play continues at 2-3-0, after the Toyland boss at 3-1-0; `$D712` ends practice when the
  camera X (`$FA84`) passes `$E30` *(inferred, not played through)*. Practice also uses its own
  hallway door table (`$7AB74`) and story text ("YOU MUST FIND THREE GEMS...").

### 2.4 Level-select technique (verified for all 28 levels)

`tools/re/coi_game_levelselect.py`, `start_level(md, index, hallway=True, story=False, difficulty=None)`:

1. Power on. Wait until `$FFF300 == $06` (opening story, frame ~337).
2. Wait until `$FFF308 == 0` (the pad is read again; mode inits write `$FF00` to `$F308/$F309`),
   press START until mode becomes `$12` (title). Release, wait `$F308==0`, press START until the mode
   leaves `$12` → `$1C` → `$08` → **`$0A` (OPTIONS, ~frame 460)**. (Edge-triggered: if a press lands
   while a screen is being built it is lost – retry.)
3. On OPTIONS write **`$FFFF42 = index`** (and optionally `$FFFF0D` = difficulty). Hold START until
   the mode changes. The game's own START handler (`$9FCC`) then computes `$FF44/$FF46`, copies the
   difficulty, initialises continues/`$FF41` (`$A094`) and sets mode `$0C` (or `$28` for level 0).
   This is exactly what the developers' level select did (§2.6).
4. Immediately (same frame boundary, before the next main-loop pass) overwrite `$FFF300`:
   * round-first levels, `hallway=True`: `$20` → hallway cut-scene with the round's door → the
     cut-scene itself sets `$0C`. This is how the game enters every round after a boss.
   * level 0 with `story=True`: leave `$28` (old-man story → hallway → level), = a real new game.
   * everything else: `$0C`. Mid-round acts are entered by the game from `$0C` directly, too.
5. Level init (`$0C`) on this first init (`$F415` bit0 clear) sets tries `$F324=3`, next extra try
   `$F282=40000`, power = 3 (Normal) / 2 (Hard) / 5 (Practice), requests the level music, then
   mode `$0E`. Mickey drops in from the top-left; he is controllable ~60 frames later.

Result (`python tools/re/coi_game_levelselect.py --states`, Normal): every level reaches mode `$0E`
with the right round/act, the level's music ID in the Z80 music slot `$1C0C` (= table `$96B0`),
power 3, tries 3, items 0, score 0 (a few levels print power 2 because Mickey got hit during the
test walk); walking right 120 frames (jumping now and then) moves Mickey (X before→after printed,
e.g. 1-1 48→205, 2-1 50→214, L-B 224→366; only 4-1-2 is blocked by the milk bottle 1168→1175).
Frames from power-on: ~530-560 for mode-`$0C` starts, 1404-2211 through the hallway, 3787 with
the old-man story. See `tools/re/coi_game_levelselect_grid.png` (start/walked pairs for all 28).
Difficulty test: `start_level(md, 9, difficulty=1)` → power 2, continues 1; `difficulty=2` →
power 5, continues 5.

### 2.5 Warping from a running game
`warp_level(md, index, hallway=True)`: set `$FFFF42`, `$FFF340=0`, then `$FFF300=$0C` – verified
1-1 → L-B, 1-B, 4-1-1, 4-T-3 (right round/act/music, keeps tries/score/items). For the hallway:
**zero the object RAM `$FFD000-$FFDFFF` first**, set `$FFFF44` = round, `$FFF300=$20` (without the
clear the leftover level objects block the hallway Mickey and it never ends – verified both ways).
Do such writes when the main loop is idle (68000 spinning in `$95C2`, `$FFF400==1`) or at VBlank
entry `$CF92`; the tools DLL's frame boundary is mid-frame (PC inside game code), where a handler
could still overwrite `$F300` (did not happen in tests).

Power on level init (`$96CC`, not the first init): after a death (`$F40F`) or a round clear
(`$F414`) or any init not caused by an act exit (`$F423==0`) power becomes
max(power, 3 Normal / 5 Practice), Hard sets exactly 2; after a normal act exit power is kept.
Tries: after a death with `$F324==0` (continue) tries become 3.

### 2.6 Built-in level select (developer leftover) – found and re-enabled
The routine at **`$9F6E`** (unreferenced) reads `$F309`: **A** = previous level (wraps 0→27),
**B** = next level (27→0), writes `$FFFF42` and draws the label from table `$A040` at plane-A VRAM
`$CB0C` (under "CONTROL" on the OPTIONS screen). It directly follows the OPTIONS handler, whose last
instruction is `$9F6C: rts`.

**ROM patch:** `$009F6C: 4E75 → 4E71` (RTS → NOP) and fix the checksum word `$00018E`
(`$4270 → $4270 + ($4E71-$4E75) = $426C`) if the patch is applied before power-on.
Now the OPTIONS handler falls into `$9F6E` every frame (except frames where it just requested a
sound-test track, which leave via `jmp $79F86`). Verified from power-on: B ×5 shows "2-1-0",
START starts 2-1 (mode `$0C` → `$0E`, round 1). All 28 labels rendered: 1-1-0, 1-2-0, 1-3-0,
1-4-0, 1-B-0, 2-1-0, 2-2-0, 2-3-0, 2-B-0, 3-1-0, 3-2-0, 3-3-0, 3-B-0, 4-1-1…4-1-5, 4-T-1…4-T-3,
4-2-0, 4-B-0, 5-1-0, 5-S-0, 5-2-0, 5-B-0, L-B-0. Side effect: B also decrements the highlighted
option (use it with the cursor on SOUND TEST). No button-code cheat exists: the only pad reads on
title/options are START edges (`$9CEE`, `$9D62`, `$9F38`) and the option handlers `$42FE`
(B = previous value, C = next value; UP/DOWN move the cursor `$F41E`).

### 2.7 What a port must set / side effects
* Nothing beyond `$FFFF42` when going through OPTIONS: difficulty, continues, tries, power, score
  are initialised by the game. For mid-round starts the game would normally carry score, items and
  current power from the previous act – set `$F22C`, `$F320`, `$F322` after mode `$0E` begins if a
  "realistic" state is wanted (HUD refresh §3.2).
* Gems: there is no gem counter; progress is implied by the round (`$FFFF44`) and level. Round-4
  door flags `$FFFF4B/$FFFF4C` only change door graphics.
* Level 0 starts with the forest music already running when entered through the story (`$F426`).
* Persistent block `$FFFF40-$FFFF4F` (continues, `$FF41` "game running", level, round, act, demo
  index, text step, door flags) is cleared only at the end of a game (`$7A76C`, `$7B2E6`, `$A232`).

---------------------------------------------------------------------------------------------------

## 3. Cheats

### 3.1 RAM map (gameplay)

| address | size | range / meaning | written by |
|---|---|---|---|
| `$FFF22C` | long | score, binary. `+100` (`$64`) per small bonus item, `+1000` (`$3E8`) power item at full power, bosses `+10000` (`$2710`, extra on Hard/Practice at `$13DEE`) | many |
| `$FFF23C` | long | score last drawn; `$9C42` redraws when `$F22C≠$F23C` | `$9C4E` |
| `$FFF282` | long | extra-try threshold: 40000 at first init (`$9710`), +50000 (`$C350`) each time (`$9C74`, also in tally `$7AAF2`); SFX `$C4` | |
| `$FFFF04` | long | high score (50000 at boot); updated at game over (`$985E`) and tally | |
| `$FFF320` | word | ITEMS 0..30: pickup +1 (`$C4E`), bag +10 (`$C30`), throw −1 (`$B8CA`, only if >0); cleared at round clear (`$7A92C`), game over (`$9866`), Hard death (`$975E`) | |
| `$FFF322` | word | POWER 0..5: hit −1 (`$92F4`), power item +1 (`$C10`, max 5), pit/kill-zone → 0 (`$BD6A`) | |
| `$FFF324` | word | TRIES 0..10 (HUD clamps to 10 in `$9BE4`): −1 at the start of every death (`$9300` power 0, `$BD6E` pit/kill-zone), +1 extra try item (`$C24`, max 10), +1 per score threshold | |
| `$FFF446` | byte | invulnerability timer: `$5B` (91 frames) after a hit (`$92EE`), `$52` on death (`$AF04`); −1/frame in `$B794`, sprite hidden on even values | |
| `$FFF447`, `$FFF450` | byte | other "can't be hit" flags (door/exit sequences) | |
| `$FFFF40` | byte | continues; −1 at each game over (`$7A590`); `$FF41 = (continues ≠ 0)` | |
| `$FFFF0F`/`$FFFF0D` | byte | difficulty master / working copy (0 Normal, 1 Hard, 2 Practice) | OPTIONS exit `$9FCE`, level init `$98F2` |

Death sequence: power 0 → `$92FA` sets `$DC31` bit2 and `TRIES--` (`$9300`); pits/kill tiles
(`$BFF8` tile type 7 → `$DC31` bit2, or `$BF62/$CA64` → SFX `$A6`) go through `$BD5E/$CA7E` →
`$BD6A: clr POWER; TRIES--`. `$AEB0` starts the death animation (music `$8A`). When it ends
(`$B1AA`): `TRIES==0` → `$F449=1` (game over), then `$F340=-1` (fade), `$F40F=1`. So TRIES is the
number of *extra* lives: dying with "TRIES x00" is game over. (Poking TRIES=0 and dying makes it
`$FFFF`, which the HUD routine clamps to 10 – a harmless quirk.)

### 3.2 HUD refresh (for RAM cheats)
`$9B4A` (end of every gameplay frame) copies HUD tiles to VRAM when bits of **`$FFF409`** are set:
bit0 power (5 words at `$FFF244`: `$807C` full star, `$807D` empty), bit1 tries (long `$FFF234`),
bit2 items (long `$FFF238`), bit3 score. Two-digit numbers are `($8010+tens)<<16 | ($8010+ones)`
(`$9C06`). Score needs nothing – `$9C42` notices the change itself, **and awards extra tries for
every `$F282` threshold crossed** (setting score 123450 gave +2 tries), so move `$F282` past the new
score if that is not wanted (`set_score()` does). `coi_game_cheats.py`: `set_power`, `set_tries`,
`set_items`, `set_score` (verified: HUD showed 5 stars, x09, x30, 00123450).

### 3.3 Cheat implementations

All ROM patches are words at even addresses; `apply_patch()` checks the original value. **If the
patched ROM is booted, add the sum of (new−orig) to the checksum word `$18E`** (cold boot compares
the sum of `$200..$7FFFF` with `$18E` and dead-loops at `$4CE` on mismatch – observed) or apply the
patch after the boot check. RAM cheats: write once per frame while `$FFF300 == $0E`.

**Infinite power** (hits still knock Mickey back and give the 91-frame blink)
```
0092EE  move.b  #$5B,$F446.w     ; invulnerability timer
0092F4  subq.w  #1,$F322.w       ; POWER--        <- patch 5378 F322 -> 4E71 4E71
0092F8  bne.b   $9308
0092FA  bset    #2,$DC31.w       ; power 0 -> dead
009300  subq.w  #1,$F324.w       ; TRIES--
```
RAM alternative: `set_power(md, 5)` whenever `$F322 < 5` (HUD refreshed). Verified: baseline in
1-3 walking right = power 3→1 (two hits, `$F446=$5B` written by `$92EE`); patch = 3→3 (hit and
knock-back still happen); RAM freeze = power 5.

**Invincibility** (no damage and no knock-back from enemies, projectiles, bosses, spikes)
All damage goes through `$92EE`; entry points: `$92B2` and `$92D2` (enemy contact checks, 68
callers, both start `moveq #0,d7; tst.b $F446.w; bne ...`), `$B522` and `$BF70` (direct/spike damage,
both `tst.b $F446.w; bne skip`).

| address | original | patched | effect |
|---|---|---|---|
| `$0092B4` | `4A38` | `4E75` | `tst.b $F446` → `rts`: returns d7=0 "no hit" |
| `$0092D4` | `4A38` | `4E75` | same for the second check |
| `$00B526` | `6604` | `6004` | `bne` → `bra`: skip `jmp $92EE` |
| `$00BF74` | `6604` | `6004` | `bne` → `bra`: skip `jsr $92EE` (spike bounce kept) |

RAM alternative: write **`$FFF446 = 3`** every frame (odd so the blink routine keeps the sprite
visible, ≥2 so it is still non-zero when enemies test it after `$B794` decremented it). Side effect:
one boss object (`$10F9E`) changes state while `$F446≠0` *(inferred)*; the ROM patch has no such
effect. Verified: patch and RAM variant both keep power 3 and never write `$F446=$5B` in the 1-3
walking-tree test. **Pits / kill-zones are not damage**: with the patch Mickey still dies in the 1-3
pit (TRIES 3→2) – protect against pits with infinite tries instead (a pit-proof Mickey would fall
forever).

**Infinite tries**
| address | original | patched | |
|---|---|---|---|
| `$009300` | `5378 F324` | `4E71 4E71` | death by power 0 |
| `$00BD6E` | `5378 F324` | `4E71 4E71` | pit / kill-zone / crush (`$BD5E`, `$CA7E` both reach `$BD6A`) |
RAM alternative: `set_tries(md, 3)` whenever it differs (the decrement at death start is undone
before `$B1AA` tests for game over). Verified with the 1-3 pit (hold RIGHT, C every 40 frames,
death ~frame 270): baseline TRIES 3→2 (write from `$BD6E`), patch 3→3 (no write), RAM 3→3; level
restarts normally (`$0C`→`$0E`).

**Infinite items**: `$00B8CA: 5378 F320 → 4E71 4E71` (throw no longer decrements). RAM:
`set_items(md, 30)`. Verified: 4 throws with B: baseline 5→1, patch 5→5.

**Infinite continues**: `$07A590: 5338 FF40 → 4E71 4E71`. RAM: `$FFFF40 = 3` before a game over.
Verified: game over with TRIES=1 + pit → modes `$18,$1A`, continues 3→2 baseline, 3→3 patched.

**Max power at (re)start**: `$00972A: 7003 → 7005` (Normal start value 3→5) and `$009746: 0002 →
0005` (Hard `move.w #2,$F322` → 5). Verified: 2-1 starts with 5 stars. RAM: `set_power(md,5)` when
mode becomes `$0E`.

**Built-in level select**: `$009F6C: 4E75 → 4E71` (§2.6). "All levels unlocked" is not applicable
(the game is linear; use the level select).

Continue mechanics: game over score is cleared (after updating the high score), continuing restarts
the level where Mickey died with 3 tries and 0 score. Screen: hold LEFT = continue, RIGHT = quit.

### 3.4 Verification output (`python tools/re/coi_game_cheats.py`)
```
baseline enemy hit           power 3 -> 1   $F446=$5B written by ['0x92ee']
ROM infinite_power           power 3 -> 3   $F446=$5B written by ['0x92ee']
ROM invincible               power 3 -> 3   $F446=$5B written by -
RAM invincible ($F446=3)     power 3 -> 3   $F446=$5B written by -
RAM power freeze 5           power 3 -> 5   $F446=$5B written by ['0x92ee']
baseline pit                 tries 3 -> 2  tries writes [('0xbd6e', 2)]
ROM infinite_tries pit       tries 3 -> 3  tries writes []
RAM tries freeze 3 pit       tries 3 -> 3  tries writes [('0xbd6e', 2)]   (restored by the cheat)
ROM invincible pit           tries 3 -> 2  (pits still kill)
baseline items               items 5 -> 1
ROM infinite_items           items 5 -> 5
baseline game over           continues 3 -> 2  modes ['0x18', '0x1a']
ROM infinite_continues       continues 3 -> 3  modes ['0x18', '0x1a']
max_power_start patch        power at start of 2-1 = 5
RAM set + HUD refresh        power=5 tries=9 items=30 score=123450
debug level select patch     options label 2-1-0; START -> mode E level 5 round 1
```
Screenshots: `tools/re/coi_game_cheats_grid.png`.

### 3.5 Difficulty (master `$FFFF0F`, working copy `$FFFF0D`; OPTIONS: B/C change, 0..2 wrap)
| | NORMAL (0) | HARD (1) | PRACTICE (2) |
|---|---|---|---|
| continues `$FF40` (`$A094`) | 3 | 1 | 5 |
| power at (re)start (`$96CC`) | max(cur,3) | exactly 2 | max(cur,5) |
| items on death | kept | cleared (`$975E`) | kept |
| other | | bosses: different attack/phase thresholds (`$133DA`, `$13942`, `$13A80` test `$FF0D`), boss defeat bonus +10000 twice instead of once (`$134CE`, `$13DEE`); object spawner gets `$FF0D` (`$6D532`, layout variant *(i)*) | same boss rules as Hard; short game: level remap after rounds 1/2, ends in round 3 (§2.3), different hallway/story text, extra art in 2-3 (`$95F4`) |
Demo mode forces difficulty 0 (`$98FE`).

---------------------------------------------------------------------------------------------------

## 4. Sound

### 4.1 Architecture
Z80 driver = a Sega **SMPS-Z80** variant, uploaded once at boot by `$79EE0`:

| ROM | → Z80 | size | content |
|---|---|---|---|
| `$784B0` | `$0000` | `$EF0` | driver code |
| `$793A0` | `$0F00` | `$880` | SFX pointer table (`$0F00`, 39 × LE word, IDs `$A0-$C6`), "special SFX" table (`$0F4E`, 1 entry), FM voices / SFX data `$0F50-$177F` |
| `$79C20` | `$1900` | `$2C0` | extra data block (header of ID `$92`) |
| `$79F76` | `$1C00` | 15 | variables init: bank `$1C00-$1C01 = $0007` → 68000 bank base `$070000`, `$1C02 = $8000` master table, timer A `$1C04 = $00B4`, timer B `$1C06 = $E6` |

Music data lives in the 68000 ROM bank `$070000-$077FFF` (Z80 window `$8000-$FFFF`). Master table at
Z80 `$8000` = ROM `$070000`: `+0` priority table `$803E` (ROM `$7003E`, 79 bytes for IDs `$81-$CF`),
`+2` special-SFX table `$0F4E`, `+4` music table `$808D` (ROM `$7008D`, 31 LE words, IDs `$81-$9F`),
`+6` SFX table `$0F00`.

Timing: the main loop (`$006F`) polls the YM2612 status: **timer B** (reg `$26` = `$1C06`, ≈128 Hz)
→ `$070F` pause check, `$04DB` command check, `$0100` SFX tracks; **timer A** (regs `$24/$25` from
`$1C04`, ≈63 Hz for `$B4`) → tempo counter `$1C16/$1C17` → `$00D9` (fade `$0783`, commands, music
tracks `$1C40`, `$1C70..`). In between it runs `$086D`: moves queue slots `$1C0A`, `$1C0B` into
`$1C09` if their priority (table) ≥ current `$1C1C`; `$1C1B` = last accepted request.

Tracks: 20 × `$30` bytes at `$1C40`: `$1C40` + `$1C70..$1D60` music FM/DAC (6), `$1D90/$1DC0/$1DF0`
music PSG, `$1E20/$1E50` special SFX, `$1E80..$1F70` SFX (6). Byte +0 flags (bit7 playing, bit2
overridden by SFX, bit4 rest), +1 channel, +3/+4 data pointer (LE).

### 4.2 How the 68000 requests sounds
`move.b #id,$F40E.w` then `jsr $79F86` (or `jsr $0476.w`, a RAM-independent `jmp $79F86` vector).
`$79F86`: SR=`$2700`, Z80 BUSREQ (`$A11100`) and wait, then **ID < `$A0` → Z80 `$1C0C`**, else
**Z80 `$1C0A`**; release bus, SR=`$2300`. One request per call; nothing is buffered on the 68000 side.
Level music: `$9686` plays `table[$96B0 + level]` at every level init, except: TRIES ≠ 0, level 2
and `$F424` bit0 set (exit touched while a fade was already running, `$06CE`) – then the 1-1
forest music simply keeps playing into 1-3. Object music zones: object `$16EE` plays `$86`/`$94`/`$85`
(subtype `$29` = 0/1/2; subtype 1 only after the special item flag `$F443`) when touched, remembering
the current zone track in `$F425` *(mechanism from code; zones not located in play)*.

### 4.3 Commands and IDs (driver `$04DB` dispatch, verified by requesting every ID)
* `$81-$9F` music (via `$1C0C`, `$0532`): starts 5-9 music tracks. **`$8D, $8F, $95, $97` have null
  pointers** – they start garbage tracks from Z80 address 0: never use.
* `$A0-$C6` SFX (`$05F0`, table `$0F00`): each starts 1-2 SFX tracks; `$BD` starts nothing.
* `$C8` special SFX (table `$0F4E`, same data as `$B3`). `$C7` (index −1) and `$C9-$CF` read past
  the one-entry table: `$C7/$CA/$CB/$CE/$CF` corrupt all 18 tracks / derail the Z80, `$C9/$CC/$CD`
  garbage – never use.
* `$D0` fade out music (`$1C0F = $28` steps), `$D1` stop everything (driver re-init `$07D5`, keeps
  bank), `$D2` silence PSG, `$D3` stop the special-SFX tracks. `$D4-$D8` jump through garbage entries
  of the command table – never use; `$D9-$FF` act like `$D1`; `<$81` ignored.

### 4.4 Music list

| ID | sound test | where the game plays it |
|---|---|---|
| `$81` | 01 | opening story + title (mode `$04`) |
| `$82` | 02 | Enchanted Forest 1-1-0, 1-3-0 |
| `$83` | 03 | spider-web cavern 1-4-0; inside the tea cups 4-T-1..3 |
| `$84` | 04 | Enchanted Forest act 2 (1-2-0) |
| `$85` | – | music-zone object subtype 2 |
| `$86` | 05 | music-zone object subtype 0 |
| `$87` | 06 | Toyland 2-1-0 |
| `$88` | 09 | Toyland 2-2-0, 2-3-0 (also the first attract demo) |
| `$89` | 16 | round clear / gem tally (mode `$24`) |
| `$8A` | 17 | Mickey loses a try (death, `$AF0A`) |
| `$8B` | 10 | The Storm 3-1-0 |
| `$8C` | 11 | The Storm 3-2-0, 3-3-0 |
| `$8E` | 12 | Library shelves 4-1-1..4-1-5 |
| `$90` | 18 | ending: reunion + staff credits (mode `$16`, object `$89F4`) |
| `$91` | 13 | dessert land 4-2-0 |
| `$92` | – | never requested; header in Z80 RAM `$1900` (driver does start 9 tracks) |
| `$93` | 07 | special-item jingle (object `$0832`, with SFX `$C3`, sets `$F443`) |
| `$94` | 08 | music-zone object subtype 1 (after the special item) |
| `$96` | 14 | Clock Tower 5-1-0, 5-S-0 |
| `$98` | 15 | Clock Tower 5-2-0 |
| `$99` | 19 | boss defeated / gem appears (6 boss objects, sets `$F451`) |
| `$9A` | 20 | final battle with Mizrabel L-B-0 |
| `$9B` | 21 | Mizrabel event during the final battle (`$10890`, `$F421`) *(inferred)* |
| `$9C` | 22 | not requested by any code found |
| `$9D` | 23 | game over (mode `$18`) |
| `$9E` | 24 | castle: old-man story (`$28`) and hallway (`$20`) |
| `$9F` | 25 | boss battles (1-B, 2-B, 3-B, 4-B, 5-B) |

### 4.5 SFX list (`$A0-$C8`)
Meaning from the requesting code; *(i)* = inferred from context only.

| ID | requested at | meaning |
|---|---|---|
| `$A0` | `$CC48` | Mickey throws an item (right after ITEMS−1) |
| `$A1` | `$CE2E`, `$10AC4` | Mickey movement *(i)* |
| `$A2` | `$C848`, `$7762` | **jump** (verified: every C press) |
| `$A3` | `$930E`, `$BD5E` | **Mickey hurt** / fell (pit) |
| `$A5` | `$B712` | Mickey state change (round 4) *(i)* |
| `$A6` | `$CA72` | kill-zone / crush death |
| `$A7` | `$BFDC` | hazard tile *(i)* |
| `$A8` | 10 enemy objects after the stomp check | **enemy defeated** |
| `$A9, $AB, $AC, $AE, $B3, $B5-$B8, $BB` | object code | enemy / boss effects *(i)* |
| `$AA` | `$11E08` | boss appears *(i)* (heard at 1-B start) |
| `$AD` | `$0648`, bosses | Mickey grabs / boss sound *(i)* |
| `$B0` | `$161A`, `$C016` | bonus / flag `$F44B` *(i)* |
| `$B1` | `$B30E` | Mickey state reset *(i)* |
| `$B2` | 12 object callers | enemy projectile / hit *(i)* |
| `$B4` | `$B5CA` | Mickey death animation start *(i)* |
| `$B9` | `$865C` | ending magic (repeated in mode `$2E`) |
| `$BC`, `$BE` | boss objects | boss hit *(i)* |
| `$C0` | `$0C58/$0C72` | **item pickup** |
| `$C1` | `$979C` | **pause jingle** (§4.6) |
| `$C2` | `$07AC` | warp item (mode `$10` wave) |
| `$C3` | `$0838` | special item (with music `$93`) |
| `$C4` | `$9C84` | **extra try** (score threshold) |
| `$C5` | `$05D8` | object *(i)* |
| `$C8` | `$D896` | raster effect object *(i)* |
| `$A4, $AF, $BA, $BD, $BF, $C6` | – | not requested by the game (`$BD` silent) |

### 4.6 Pause handshake (verified with the fake-timer driver, §4.8)
`$9770` on START: pause → Z80 `$1C0A = $C1` and `$1C13 = 1`; resume → `$1C12 = $80`, `$1C13 = 0`.
SFX `$C1` data (Z80 `$1606`) starts with coordination flag `$FC 04` (handler `$0A12`: suspend all
music tracks), plays a ~70-frame jingle, then `$FC 05` (restore music track flags) and `$FC 01`
(`$1C12 = 1`: `$070F` silences everything and from then on skips every update, state 2). Resume:
`$1C12 = $80` → `$0720` clears it and the music continues from the same position.
Observed: paused +20 f: only SFX1 active; +100 f: `$1C12=2`, music tracks frozen at the same
pointers; START again +20 f: `$1C12=0`, pointers advancing.

### 4.7 Sound test
OPTIONS row 2 (`$F41E==1`), B/C change `$F41F` 0..24 (shown as SOUND 01..25) and the handler
(`$9F46`) immediately requests `table[$9F1E + $F41F]` (only when it differs from `$F40E`);
on the other rows it requests `$D1`. Table: `81 82 83 84 86 87 93 94 88 8B 8C 8E 91 96 98 89 8A 90
99 9A 9B 9C 9D 9E 9F`. Verified by stepping 01→25 on the real OPTIONS screen: `$F40E` and Z80
`$1C0C` matched the table for all 25. The sound test only covers music; valid SFX are `$A0-$C6`,
`$C8` (§4.3), commands `$D0-$D3`.

### 4.8 `play_sound(md, id)` / `stop_sound(md)` (`tools/re/coi_game_sound.py`)
`play_sound`: ID < `$A0` → Z80 `$1C0C`, else Z80 `$1C0A` (exactly `$79F86`); `slot2=True` uses
the second queue slot `$1C0B`, which the game never writes, so a port's own SFX can't clobber (or be
clobbered by) a game request made in the same frame. `stop_sound` = `$D1`, `fade_out` = `$D0`,
`set_pause` mirrors `$9770`. From Python the writes happen between frames (no BUSREQ needed); a
real-time port should do BUSREQ/write/release like `$79F86`, or write at a frame boundary. Setting
`$FFF40E` too is optional (only the OPTIONS screen reads it back).

**Verification in this DLL:** the YM2612 is stubbed and its status never reports a timer overflow,
so the driver idles forever in `$006F-$007C` (`$086D` still moves queue slots into `$1C09`, e.g.
`$1C09=$D1` from boot, but nothing is processed; level music requests stay in `$1C0C`).
`enable_fake_ym_timers()` patches the *Z80 RAM copy* only: `$0077`/`$0092` `ld a,($4000)` →
`ld a,($1800)`; the timer-reset calls at `$0082`/`$0099` go through stubs at `$1810`/`$1820` that
clear the fake bit; `run_ticking()` sets both bits once per frame. Not needed with a real YM2612.
Results (`python tools/re/coi_game_sound.py`):
* boot: `$1C09=$80` idle; `play_sound($82)` → 8 music tracks, pointers advancing over time;
  `$9F` replaces it; `$C0` adds SFX1; `$D0` fade counter `$25…$00` then silent; `$D1` silent.
* every ID `$81-$D3` requested from a quiet driver: see §4.3 (valid ones start tracks whose
  pointers move within 90 frames).
* in-game (1-1 state): the pending level request `$82` is consumed and its tracks play; pause /
  resume as §4.6; a C press requests `$A2` (SFX1 starts); `play_sound($9F)` mid-game switches the
  music; `play_sound($C4, slot2=True)` plays; `stop_sound` silences.
* sound test 01..25 → IDs match `$9F1E`.

---------------------------------------------------------------------------------------------------

## 5. Other port notes

* **Pause**: `$FFF41D` bit0, toggled only by a START edge in mode `$0E` (`$97E2`). While set the
  gameplay handler returns after `$187C` (sprites still drawn); the Z80 pause handshake is §4.6.
  A port pause menu can either stop emulation or set `$F41D` + `set_pause()`.
* **Controller**: VBlank `$AE4E` reads port 1 (3-button, TH toggling) into `$F308` (held) and
  `$F309` (new presses, `new & ~old`), format `S A C B R L D U`, 1 = pressed. `$95CA` writes `$FF00`
  on screen changes so a held button needs a release first. Gameplay logic reads `$F310/$F311`
  (`$CE3C`): reversed-controls flags `$F441`, then control-config remap `$FF0E` (table `$CF30`):
  0 = A shot / B shot / C jump, 1 = A jump / B shot / C shot, 2 = A shot / B jump / C shot.
  Demo mode skips the remap.
* **RNG**: 32-bit seed `$FFFF00`, cleared at cold boot. `$D506` steps it every non-lag VBlank
  (`t = ((4x+1+x)<<3)+x`, then the low word+1 and the high word are swapped/added into the new
  seed; seed 0 is replaced by `$2A6D3651`); `$958E` (`Random()`, 23 callers, same mixing without
  the +1s, seed 0 → `$2A6D365A`, result in d0) steps it on demand. The game is fully
  deterministic from power-on given the same input frames; a port can randomise by writing a
  non-zero `$FFFF00` once.
* **Demo (attract)**: flag `$F40A`; demo index `$FFFF48` cycles 2,4,0 → levels 6 (2-2), `$19`
  (5-2), 2 (1-3) with input streams at ROM `$69740`, `$69864`, `$6990E` (compressed, unpacked by
  `$7FCEE` to `$FFE000`, 1500 frames, one held-byte per frame; `$9954` feeds `$F308/$F309`,
  `$F326` = position). Score shown = high score. START → mode `$04`; end → `$F422` → mode `$00`.
  **Trigger at will**: on the title or OPTIONS write `$F40A=1`, `$F300=$1C` (verified: copyright
  screen, then level 6 demo, then back to SEGA after 1500 frames).
* **Options variables**: `$FF0D/$FF0F` difficulty, `$FF0E` control, `$F41E` cursor row,
  `$F41F` sound number. All in the persistent block (survive the attract loop).
* **Region**: `$FFFF0C` = `$A10001` at boot; PAL selects 240-line VDP mode values in `$FF08/$FF0A`.
* **Level exit timing**: exits wait for the palette fade (`$F340`: 1 = fade in requested, −1 =
  fade out, 0 idle; `$7C2A2`).
