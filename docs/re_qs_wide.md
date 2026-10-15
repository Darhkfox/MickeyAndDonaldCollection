# QuackShot (World, REV 00) — widescreen RE notes

Goal: run the unmodified game logic on a VDP that renders E extra pixels on each side
(E = 40 → 400x224, E = 20 → 360x224) and keep the extra columns correct.

Scripts (all in `tools/re/`):
- `qs_wide_util.py` — helpers: pinned reference core, `goto_area()`, layer struct reader,
  nametable-vs-map checker `check_plane()`, test-only camera pan hack, HUD tile renderer.
- `qs_wide_patch.py` — the ROM patch list (`patches(E)`, `apply(md, E)`, `apply_bytes(rom, E)`).
- `qs_wide_demo.py` — applies the patches, plays sections at E=40, writes before/after strips.
- `qs_wide_verify.py` — automated checks (streaming, sprites, spawn, edges), see §10.

> Tooling note: `tools/mdt.py` prefers `build/tools/mdtool.dll` when it exists (another agent
> builds one) and that build cannot load `tools/states/*.state`. `qs_wide_util` pins
> `tools/bin/mdtool.dll` via `MDTOOL_DLL` unless the variable is already set.

## 0. Game structure (what's needed to get around)

| RAM | meaning |
|---|---|
| `$FFFF18` (word) | main-loop state; low byte indexes the table at `$4AE` (bit 15 = "initialised" flag used by some states). Main loop `$490`, frame wait `$9550`. 0 boot/Sega, 1 attract/title (`$A3D6`), 2 world map (`$9BE8`), 3 arrival → level load (`$461C`), **4 level load** (`$464C`), **5 gameplay** (`$477A`), 6 `$4974`, 7 `$4A0C`, 8 START menu = weapon/item select (`$7AA`), 9 ending scene (`$A2D8`), A Duckburg departure plane cutscene (`$A0FC`), B title menu START/OPTIONS (`$4A5C`), C story text (`$4FE2`), D checksum-fail screen, E → world map (`$4604`), F game over (`$487E`), 10 continue screen (`$48D0`) |
| `$FFFF1A` (word) | VBlank handler index (table at `$582`): 0 = map/logo, 1 = gameplay (`$5D2`), 2 = game over/continue (`$5D6`), 3 = attract, story, cutscenes (`$618`), 4 = START menu (`$76C` path), 5 = title menu (`$732`) |
| `$FF8F98` (word) | lives; `$FFEE58` ≠ 0 = player died (`$4816`) |
| `$FF8F34` (word) | location 0..9: 0 Duckburg (variant), 1 Duckburg, 2 Mexico, 3 Transylvania, 4 Viking ship, 5 South Pole, 6 Maharajah's palace (India), 7 Egypt, 8 Pete's hideout / mine, 9 the island (Great Duck Treasure) |
| `$FF8F74` (word) | area within the location (area counts: 1,3,4,7,5,4,9,6,5,4) |
| `$FF8F36` (word) | area "setup" index read from the area header (selects the area init routine in the per-location table at `$35DE`) |
| `$FF8F30` (word) | plane-B parallax mode (see §1) |
| `$FFEF48` (word) | per-area effect/exit routine called every frame via `$296A` (`jmp (a3)`) |

Jumping to any area from gameplay: `set16($FF8F34, loc); set16($FF8F74, area); set16($FFFF18, 4)`
and run ~200 frames (`qs_wide_util.goto_area`). Used for all tests below.

Boot checksum: `$3EC..$40A` sums `$200..end` and compares with `$18E`; on mismatch `bne.w $502`
(error screen, hangs at `$520` — confirmed in the core). Any ROM patch needs either the checksum word at `$18E` recomputed or the branch
disabled: **patch `checksum`: `$40A`: `66 00 00 F6` → `4E 71 4E 71`**.

## 1. Camera

Two 64-byte "layer" structs, one per plane:

| offset | plane A `$FF8500` / plane B `$FF8540` |
|---|---|
| +$00 (long 16.16) | X max (exclusive)  |
| +$04 (long) | X min (exclusive) |
| +$08 (long) | Y max (exclusive) |
| +$0C (long) | Y min (exclusive) |
| +$10 (long) | pointer to chunk layout (256x256 chunks, 32 per row) (`$FFFB00` A / `$FFFBC0` B) |
| +$14 (long) | chunk data (16x16 metatiles per chunk) (`$FF0000` A / `$FF2800` B) |
| +$18 (long) | metatile defs, 8 bytes = 2x2 nametable words (`$FF5000` A / `$FF5800` B) |
| +$20 (long 16.16) | **camera Y** (word `$FF8520` / `$FF8560`) |
| +$24 (long 16.16) | **camera X** (word `$FF8524` / `$FF8564`) |
| +$28 / +$2C | previous frame Y / X |
| +$30 / +$34 | Y / X velocity for this frame (plane A: `$FF8530`/`$FF8534`) |

Per frame (`$2546`, called from gameplay `$477A`):
1. `$259A` camera follow: target = player X/Y (`$FFA2D0`/`$FFA2D4`, long 16.16). Dead zone
   `$FFEF18`/`$FFEF1C` (X: scroll right when target−X ≥ $8C, left when < $7C) and
   `$FFEF20`/`$FFEF24` (Y: $78/$68). Adds to velocity `$8534`/`$8530`. **Skipped when
   `$FFEE56` (X) / `$FFEE57` (Y) ≠ 0** (camera locks, see §4).
2. `$2656` apply: old = pos; pos += vel; if not (min < pos < max) the move is undone and the
   velocity zeroed (long compares, so the clamp keeps the camera strictly inside, and an edge is
   never re-reached exactly once left).
3. `$26B4` tile streaming (§2), `$296A` per-area effect (`$FFEF48`).
4. Plane B: `$25E0` derives plane-B velocity from plane A's by mode `$FF8F30`:
   0 none, 1 X/2 Y/2, 2 X Y, 3 X/2 Y, 4 X/32 Y/2, 5 X/2 Y/32; then `$2656`/`$26B4` for plane B
   with its own bounds.
5. `$27EE` builds the per-line H-scroll table (only in line-scroll areas, §7). `$2586` clears
   both velocities.

Bounds come from the area header (`$35B6`: `move.w (a0)+` into +$00, +$04, +$08, +$0C); level
width = Xmax + 320, height = Ymax + 224. Some effect routines change Y max at run time
(`$30B2`, `$30C0`, `$3172`, `$3180`: `$8508` = $120 / $320).

VBlank (gameplay handler `$5D2` → `$2890`):
- VSRAM: plane A V = `$8520` − `$8F88` (shake), plane B V = `$8560`.
- H scroll at VRAM `$D000` (reg 13 = $34). If `$FFEE01` bit 0 = 0: full-screen mode (reg 11 = 0),
  A = −(`$8524` − `$8F86`), B = −`$8564`. If bit 0 = 1: reg 11 = 3 (per line) and the 224-line
  table built in RAM at `$FFF700` is DMA'd to `$D000` (`$2938`).
- `$8FB2`/`$8FB4` get copies of the Y/X used for this frame.
- VRAM layout (gameplay): plane A `$C000`, plane B `$E000` (64x32 cells, reg 16 = 1), window
  `$D000` (reg 3 = $34), SAT `$D400` (reg 5 = $6A, DMA from `$FFF300`), H scroll `$D000`.

## 2. Tile streaming

Original behaviour (`$26B4`, one call per plane, a1 = layer, d6 = VRAM base `$C000`/`$E000`):
- **Columns**: when X crosses a 16-px boundary: moving left → column at X; moving right →
  column at oldX + $200. Column routine `$2786` fills 16 metatiles (= the whole 256-px plane
  height, starting at the metatile row containing Y) into queue `$FF8702` (A) / `$FF8886` (B).
- **Rows**: when Y crosses a 16-px boundary: moving up → row at Y; down → row at oldY + $100.
  Row routine `$271C` fills 32 metatiles (= the whole 512-px plane width, starting at X) into
  queue `$FF8600` (A) / `$FF8784` (B).
- VBlank `$28DA` flushes one column (32 rows x 2 cells, wrapping with mask `$EFFF`) and one row
  pair (64 cells x 2 rows, wrap mask `$FF7F`) per plane.
- Initial fill at area load: `$3F96`/`$3FBC` = 16 rows starting at (X, Y), flushed immediately.

So the plane always holds map columns **[X, X+512)** and rows [Y, Y+256): the right side has
192 px of slack, the left side none. At E > 0 the left extension shows the columns that are
512 px further right (clearly visible in Duckburg: a different building left of the start).
Vertically nothing changes (rows are full width, columns full height, the view is still
224 lines of which 192 are above the status bar).

Fix: move the valid window left by OFF px: keep [X−OFF, X−OFF+512). Correct for any E with
E ≤ OFF ≤ 177−E (16-px column granularity). **OFF = 96 centres the window on the screen centre
and works for every E ≤ 80** (E=40: ≥56 px margin left, ≥41 right; E=20: ≥76/61). The patch
is therefore independent of E.

The start of the window is clamped at 0 (`max(X−96, 0)`): map columns at negative X are read
from outside the chunk table (garbage, mostly black tiles). Without the clamp this garbage is
visible in the **line-scrolled background bands** (Mexico, South Pole ice area): those bands
scroll plane B by values unrelated to plane B's camera X and therefore show the whole 512-px
plane row (the game relies on those map rows repeating every 512 px). With the clamp the plane
always holds 512 consecutive, valid map columns, exactly like the original near X = 0.

| patch | addr | original | new |
|---|---|---|---|
| `stream_col` | `$26C8` | `6D0E 3029 0024 3229 0020 4EBA 00B2 6010 3029 002C 0640 0200 3229 0020 4EBA 00A0` | `6D06 3029 0024 6008 3029 002C 0640 0200 0440 0060 6A02 7000 3229 0020 4EBA 00A0` |
| `stream_row` | `$26FC` | `6D0C 3029 0024 3229 0020 6000 0014 3029 0024 3229 0028 0641 0100 6000 0004` | `6D06 3229 0020 6008 3229 0028 0641 0100 3029 0024 0440 0060 6A02 7000 6002` |
| `stream_init` | `$3FCE` | `4EBA E74C` (jsr $271C) | `4EBA E73C` (jsr $270C) |

New code:
```
26c4  cmp.w   $24(a1),d1        ; d1 = old X
26c8  blt.b   $26d0             ; moving right
26ca  move.w  $24(a1),d0        ; moving left: X
26ce  bra.b   $26d8
26d0  move.w  $2c(a1),d0        ; moving right: oldX + 512
26d4  addi.w  #$200,d0
26d8  subi.w  #OFF,d0           ; OFF = $60
26dc  bpl.b   $26e0
26de  moveq   #0,d0             ; clamp at 0
26e0  move.w  $20(a1),d1
26e4  jsr     $2786(pc)         ; column
...
26f8  cmp.w   $20(a1),d1        ; d1 = old Y
26fc  blt.b   $2704
26fe  move.w  $20(a1),d1        ; up: Y
2702  bra.b   $270c
2704  move.w  $28(a1),d1        ; down: oldY + 256
2708  addi.w  #$100,d1
270c  move.w  $24(a1),d0        ; <- also the new entry used by the initial fill
2710  subi.w  #OFF,d0
2714  bpl.b   $2718
2716  moveq   #0,d0             ; clamp at 0
2718  bra.b   $271c             ; row routine
271a  rts   (unchanged, target of the beq at $26F6)
```
The initial fill loop (`$3FC2..$3FE0`) now calls `$270C`, which reloads X from the layer and
subtracts OFF before entering the row routine. The same loop is used when returning from the
START menu (`$D76` → `$3F96`), so that redraw is covered too.

Other nametable writers checked: the only other code that writes map blocks straight into a
plane is the Egypt area-4 wall event (`$2F74` → `$41F6`, 12 metatile rows at plane A cells
12..13 = world x 96..111, triggered when the player passes x = $C0, i.e. camera X ≈ 50). With
the patch world x 96..111 is resident whenever X ≤ 192 (original: X ≤ 96), and the chunk data in
RAM is modified too, so later re-streaming is consistent (checked with screenshots and
`check_plane`). The other path of the same animation system (`$4144`) is tile-pattern DMA,
not nametable data (coverage over all 48 areas).

Verification (`check_plane()` in `qs_wide_util`): computes, with the game's own lookup
(chunk → metatile → 2x2 words), the nametable word every visible cell of the 320+2E view must
hold and compares with VRAM (plane A `$C000`, plane B `$E000`), using the scroll values that were
displayed that frame (layer +$2C/+$28, falling back to +$24/+$20 on lag frames), limited to
the level rectangle. Test (`E=40`): for all 48 areas (10 locations) jump in, then pan the
camera (test hack: persistent velocity + camera locks) right to Xmax at 4 px/f, down at 3 px/f,
left to 0, up to 0, diagonally (4,2) for 200 f and (−3,−1.5) for 100 f, checking every frame.
Result: **0 mismatching frames in all 48 areas with the patches (E=40, with and without the
optional camera clamp of §4); unpatched 500..2600 bad frames per scrolling area** (always the
left extension columns). Re-run: `python tools/re/qs_wide_verify.py stream [--orig] [--clamp] [--E 20]`.
A second, scroll-mode independent check (`check_displayed()`, reads the real VSRAM/H-scroll
table incl. per-line mode) was used for the line-scroll areas (loc 2 areas 0..2, loc 5 area 2):
0 garbage cells with the patch, 63 bad frames when the clamp at 0 is removed.
Screenshots (Mexico start, walking right): the original shows a rock pillar / building from
512 px ahead in the left extension, the patched build shows the correct cacti.

## 3. Objects and sprites

Object slots: 64 bytes each; 48 level slots at `$FF9000` (free-slot search `$19D7C`), player
shots at `$FF9C00` (11 slots), 16 "global" slots at `$FF9EC0`, player at `$FFA2C0`, more
player-related slots from `$FFA300`. Object +$00 type, +$0A sprite base word (priority,
palette, tile), +$0C flags (bit 3: +$10/+$14 are screen coordinates, bit 4: never culled or
despawned), +$10 X, +$14 Y (world), +$2C/+$2D X/Y draw offsets, +$2E pointer into the level's
spawn list (spawned flag).

**Spawning** (`$19B78`, every gameplay frame). The level's object list is a grid of
128x128-px cells (`$FF8000`: byte width, byte height, one byte per cell → groups of 8-byte
entries; entry byte 2 bit 0 = "alive").
- Horizontal: when camera X crosses a 128-px boundary: moving right → spawn the cell column
  containing X+$180 (`$19C16`), moving left → the column containing X−$71 (`$19C1C`); 5 cell
  rows from (Y>>7)−1.
- Vertical: same on Y (+$180 / −$71 at `$19BC0`/`$19BC6`), 5 cell columns from (X>>7)−1
  (covers X−128−r .. X+512−r, r = X&127: enough for the wide view; unchanged).
- Area start (`$19D5A`): columns X−$71 + {0, $80, …} while d7 < $280 (`$19D74`).
- So the always-spawned range is [X − r, X + 512 − r): on the right at least X+385 (objects
  appear ≥ 25 px before they become visible at E=40, 45 px at E=20 — fine), but on the **left it
  starts at X itself → objects in [X−E, X) pop in when walking left**.

**Despawn** = the sprite builder's cull (`$9A72..$9AB2`): d4/d3 = world + $80 − camera; if
either is outside [0, $280) (world X ∉ [camX−128, camX+512), Y ∉ [camY−128, camY+512)) the
object is deleted via `$B6C2` (which clears the spawn-list flag so it can respawn later),
unless +$0C bit 4 is set.

**Sprite pieces** (`$9AE0..$9BB0`): per piece Y = d3 + dy + $2D(a0) + `$8F88`, clipped by
`$9BB8` to VDP Y ∈ [$60, $160) (screen −32..223); X = d4 + dx + $2C(a0) + `$8F86`, clipped by
**`$9BCE` to VDP X ∈ [$60, $1C0) = screen −32..319** → pieces starting right of x = 320 are never
drawn. The first 4 SAT entries (`$99A0`) are fixed masking sprites (Y = $140/$160, X = 1 then
X = 0, 1x4 cells) that hide every sprite on lines 192..223 (status bar). Piece budget
`$FF8F2E` = 76, link counter `$FFEE40`, SAT buffer `$FFF300` → VRAM `$D400` by DMA in VBlank.

Patches:

| patch | addr | original | new |
|---|---|---|---|
| `spr_xmin` | `$9BCE` | `0C40 0060` (cmpi.w #$60,d0) | `0C40 xxxx`, xxxx = $60−E (E=40: `0038`, E=20: `004C`) |
| `spr_xmax` | `$9BD4` | `0C40 01C0` (cmpi.w #$1C0,d0) | `0C40 xxxx`, xxxx = $1C0+E (E=40: `01E8`, E=20: `01D4`) |
| `spawn_left` | `$19C1C` | `0440 0071` (subi.w #$71,d0) | `0440 00F1` (spawn the next cell column further left) |
| `spawn_init` | `$19D74` | `0C47 0280` (cmpi.w #$280,d7) | `0C47 0300` (6 spawn columns X−$F1..X+$18F at area start) |
| `despawn` | `$9A9A` | `0C44 0280 6400 1C22 4A44 6B00 1C1C` | `3004 0640 0080 0C40 0300 6400 1C1C` |

`despawn` new code (d0 is free at this point):
```
9a9a  move.w  d4,d0
9a9c  addi.w  #$80,d0
9aa0  cmpi.w  #$300,d0        ; world X in [camX-256, camX+512)  (was [camX-128, camX+512))
9aa4  bcc.w   $b6c2           ; delete object
9aa8  cmpi.w  #$280,d3 ...    ; Y test unchanged
```
The left despawn edge has to move out by a full cell (128 px) because the new left spawn cell
is (X>>7)−1: its objects must not be deleted on the frame they are created.

VDP limit: sprite X is 9 bits, screen x = X−128 ∈ [−128, 383]; with `spr_xmax` = $1C0+E the
largest piece X is $1BF+E, fine for E ≤ 64 (beyond that the port would need a 10-bit X). The
lower bound $60−E is > 0, so a piece can never land on X = 0 (which would act as a line mask).

Verification (E=40, `qs_wide_verify.py sprites|spawn`):
- Sprite pieces whose left edge is in the right extension (x 320..359) while walking right for
  500 frames: original 0 in all four test areas; patched Mexico 298, Transylvania 24,
  Egypt 764, India 1008 pieces. Screenshots: in the original, enemies entering from the right
  are cut at x = 320 (only the part left of 320 is drawn); patched they are complete.
- Objects spawned while already inside the 400-px view (camera panned right then left over the
  whole area): original left-side pop-ins 6 (Mexico), 6 (Transylvania), 8 (Egypt), 3 (India),
  2 (Island), 2 (South Pole); patched 0 everywhere. Remaining events (identical in both builds)
  are objects created by other objects or by the vertical trigger mid-screen, and one object
  type that parks itself at camX+$160 (table below).
- Peak number of live level objects rises slightly (e.g. 12→14, 6→8, 9→12 of 48 slots).
- Player shots (`$FF9C00`) are unaffected: 25 shots per 600 frames in both builds, same
  lifetime left and right.

Gameplay consequences: objects left behind on the left live 128 px longer (they keep moving
and acting while up to 256 px off the left edge); objects appear one cell earlier on the left.
Nothing else changes.

Object-specific camera-relative code (not patched; listed for completeness):

| code | what | at E=40 |
|---|---|---|
| `$E86E` | "is on screen" test, screen x ∈ [−32, 352), y ∈ [−16, 224); used by 19 object routines to trigger actions | objects at x 352..359 or −40..−33 are visible but count as off-screen (a few frames later reaction) |
| `$12160` (+`$11FEC`, `$11FF4`) | flying enemy (type $45, India) parks at camX+$160 (screen 352) for 300 frames before flying in | its left part is visible parked in the right extension; optional fix: `$12166: 0160→0160+E`, `$11FEE: 0160→0160+E`, `$11FF6: 0140→0140+E` (it then flies E px further) |
| `$10F0C`, `$10FEE`, `$1103C`, `$11088`, `$110FE` | objects placed at camX−$20 (screen −32) | can be visible in the left extension; optional fix: the following `subi.w #$20` immediates → $20+E |
| `$D020` / `$89A2` | objects placed at camX+$F0 / camX+$120 | inside the 320 view anyway |
| `$5956` | deletes a player shot outside [camX−64, camX+384) | covers the view up to E = 64; the port moves it out by E−64 |
| `$CD52`, `$CE92` (≥ $1A0), `$CDB0`, `$CF02` (≤ −$60), `$13A6E` (≤ −$40), `$13AD6` (≥ $180) | "far off-screen" checks | outside the 400-px view, fine for E ≤ 40 |

**Port: `$E86E` callers and other edge limits (October 2026).** `$E86E` is replaced by `jmp $7B200`: the same
test, but for these callers (matched by return address) X uses (−32−E, 352+E): `$FB7A` (bridge / platform
pieces $75/$78/$86/$8E are hidden until "on screen" — the Pete's Hideout bridges grew piece by piece at 21:9),
`$E6EC` ($0A, falls from the top and resets once off screen), `$DB60`, `$DCD0`, `$10832`, `$12258`, `$13F74`
(objects deleted once off screen). The other callers only start an enemy's behaviour and keep the original
test. Wrapping objects $3D/$48/$B3 (`$10FE4`, `$11032`, `$1107E`: wrap at camX+$160; `$10F12`, `$10FF4`,
`$11042`, `$1108E`, `$110FA`: re-enter at camX−$20) and the parked flyer $4F (`$12164`, `$11FEC`, `$11FF4`)
move out by E. Beyond E = 64 the shot delete window `$5976`/`$597E` moves out by E−64 (shots reach the visible
edge). The $19 walkers wait in plain sight where they stop, so their limits `$CD52`/`$CE92`/`$CDB0`/`$CF02` move
out by E−64 beyond E = 64 too: 32 px beyond the widened edges at 2:1 (unpatched 416 / −96) and 21:9 (460 / −140).
(Round 2 QA: with E−96 the Duckburg boxing turtles stood half visible at x 428 for 1,300+ frames at 21:9.) The
$21/$55/$B0 limits `$13A6E`/`$13AD6` move out by E−64 beyond E = 64 (21:9: −108 / 428); no visible case was found
for them, and moving them out at 2:1 as well changed when a $B0 in The Island Area 1 turned back, so they stay.

**Port: far right spawn trigger and "deferred" entries (October 2026).** From 2:1 on the right edge is X+384,
the start of the column the right trigger spawns, so the trigger also has to look one cell further,
(X+$200)>>7 (with the vertical trigger at 6 columns, `$19BEA`). But spawn entries are filed by cell, not by their
own x: some sit 3-4 cells left of their cell (birds $0D/$7C/$4B that swoop in behind Donald; e.g. Rooftops entry
`$FF80A4`, x 1888, cell column 18). Through the far column they were created 128 px early, 96-144 px inside the
view. Now `$19C16` jumps to `$7B400`, which runs the original column (X+$180) and then the far one (X+$200), and
`$19C92` (before the free-slot search in the entry loop `$19C5E`) calls `$7B420`, which tells the passes apart by
return address: `20(sp)` = `$19C52` (the column loop) and `24(sp)` = the wrapper's `$7B40C` (original column) or
`$7B418` (far column). The area-start spawner (`$19D5A`, 7 columns through `$19C08` with d2 = −1) and the left
trigger use the same loop and are left alone; a first version that picked the passes by column instead skipped
area-start entries in Transylvania Castle 1. The column is the saved d2 (`6(sp)`). An entry is *deferred* if
it sits more than 192 px left of its cell, x < column×128 − 192: at 4:3 the original pass creates it at screen
x < 192 (the swoop-in birds sit 352-416 px left, so at about −32..32), and the far pass would put it 128 px
further right, in the middle of the view. Entries only a little left of their cell (South Pole `$4B`, Gyro's
`$C9`, Transylvania `$2F`: 64-80 px) keep the far timing, which brings them in at the right edge as 4:3 does;
a first rule based on the far creation point (x < column×128 − 160 + E) deferred those too and made them pop
in ~100 px inside the right edge. In the far column deferred
entries are skipped (carry set, spawn flag untouched); in the original column only deferred entries are created
(the others were handled by the far pass a cell earlier, so a defeated enemy is not re-created). Deferred
entries therefore appear at exactly the 4:3 moment, everything else a cell early as before. The vertical
trigger's extra column (X>>7)+4 (return address `$19BFC`, column checked against `$24(a4)`) leaves deferred
entries as well, since 4:3 never creates them from there; they come with the original-column pass or a vertical
pass once their column is +3, as at 4:3. The left trigger and the area start are unchanged. Check (`tools/qa/r2_fix/qs/repro_bird.py 32 D`): the Desert vulture
is created at frame 854, screen x −1, at every width (was frame 751, x 127, at 2:1 / 21:9).
Round 3 QA census: 41 of the 1,036 spawn entries are deferred - the 15 swoop-in birds (`$0D`, `$7C`, `$4B`) and
falling things, Transylvania enemies (`$25`, `$2C`), `$0C`, `$52`, `$8B`, `$7D`-`$7F`, `$76`, `$B0` and the Ghosts
`$90-$99`; all of them now appear at the camera position and screen spot they have at 4:3.

**Switching width mid-level (October 2026, round 3).** From 16:9 or narrower up to 2:1 / 21:9 the game has created
columns up to (X>>7)+3, while from then on the original-column pass creates only deferred entries and the far pass
starts at +5, so the non-deferred entries of +4 were lost (67 of 164 tested switch points). `configure()` now notes
the width it last configured (`module_state[1]`) and after a narrow -> wide change sets `module_state[0]` and the
ROM word at `$7B45E` (`moveq #1,d6` instead of `#0`, read by the hook's original-column branch): that pass then
creates every entry of its column. `post_frame()` clears both once the camera's previous-frame X (`$852C`) is in
another 128-px cell (`$8524` is a frame early: the spawner compares the values the frame before). Check
(`tools/qa/r3_fix/qs/t_switch_spawn.py`, 36 areas x 7 switch points): 0 -> 108, 40 -> 108, 40 -> 64, 20 -> 64: 0 misses
in 656; 108 -> 40, 64 -> 0: only the Hideout `$8128` bomb bird, created and culled in the same frame at every width.

Every patch change (width, cheats) is also deferred to a safe frame end now (`patches_safe()` in `common.cpp`,
`Session::try_switch()`): the ROM words that would change must not be at the PC or behind a return address on the
stack, otherwise the code is pulled from under the 68000 (round 3 QA: a switch to 4:3 while the CPU was in `$7B200` /
`$7B000` froze the game in the exception loop). Save states record the configuration they ran with
("qs|108|cheats"), so a load restores that ROM first and moves to the current settings at the next safe frame.

## 4. Level edges / locked camera

Facts:
- Plane A's camera range is (Xmin, Xmax) from the layer struct (`$FF8504` word = Xmin, always
  0 in this game; `$FF8500` word = Xmax). Level width W = Xmax + 320. The camera is only ever
  moved by `$2656` (continuous velocity) after the area-load initialisation (`$3F14..$3F3A` from
  the area header, then the initial fill); deaths, doors and area changes all go through the
  area loader `$34E6` (state 4).
- The player is clamped to world bounds, not to the camera: X ∈ [`$FF8F6A`, `$FF8F68`] =
  [16, Xmax+304], Y ∈ [`$FF8F6E`, `$FF8F6C`] (`$5D3E..$5DA6`). So a narrower camera range never
  restricts the player.
- Camera X ranges per area (E=40 survey, `qs_wide_verify.py edges`): Xmax = 0 (single 320-px
  screen) in loc 6 areas 7/8 (Maharajah throne room / tiger); Xmax = $C0 (512-px rooms) in
  loc 2.2, 3.6, 4.2, 4.4, 6.1, 6.2, 8.4, 9.3; $1C0 in 6.3, 6.4, 7.4; everything else ≥ $2C0.
- Camera locks `$FFEE56`/`$FFEE57` (follow disabled) are only used while Donald rides/climbs
  things (`$6774..$697A`, saves/restores the flags via `$FF8FC0`) and by the Viking ship area
  inits 0 and 2 (`$3952`, `$39AA`: Y lock only, the deck does not scroll vertically); the
  camera stays inside its bounds, so they need nothing special. There are no auto-scrolling
  camera sections in gameplay (the mine carts in Egypt 5 / hideout 1 are ordinary follow-camera; the Egypt 4 and
  Transylvania 4 crushers move plane B, not the camera).

### 4a. Runtime rule (no patch, always correct) — recommended baseline

Evaluate once per frame, for lines 0..191 (the status bar lines are handled by §5):
```
X     = word $FF8FB4   (plane-A camera X used for this frame's scroll; copy made in VBlank)
Xmin  = word $FF8504
Xmax  = word $FF8500
mask_left  = clamp(E - (X - Xmin), 0, E)     ; black columns at the left edge of the output
mask_right = clamp(X + E - Xmax,  0, E)      ; black columns at the right edge
```
(`$FF8524` is equivalent except on lag frames.) Only in gameplay (§6 condition). Results: at
an area start X≈0 → left 36..40 px masked; at the right end X = Xmax−4 → right 36 px masked;
320-px rooms (Xmax = 0) → both sides fully masked (= pillarbox); everything in between: no mask.

### 4b. Optional ROM patch: keep the camera E px inside (no masking needed)

When the camera range is at least 2E+2 px, plane A's bounds test uses (Xmin+E, Xmax−E), and the
area loader moves the start position into that range before the initial plane fill (plane B is
moved by the parallax ratio of `$FF8F30` using the game's own `$25E0`). Narrower areas keep the
original bounds (and use the runtime mask). New code in the unused $FF gap `$7A79C..$7BF00`:

| patch | addr | original | new |
|---|---|---|---|
| `cam_hook_code` | `$7B000..$7B0CD` | `FF`… | code below (E enters as E<<16 and (2E+2)<<16 immediates) |
| `cam_bounds` | `$2664` | `2229 0004 B2A9` | `4EB9 0007 B000` (jsr $7B000) |
| `cam_bounds2` | `$266A` | `0024` | `6018` (bra.s $2684) |
| `cam_load` | `$35A0` | `4EBA 1A5E 4EBA 09F0` (jsr $5000; jsr $3F96) | `4EB9 0007 B056 4E71` (was `B044` before the narrow-room code was added) |

```
7b000  move.l  $4(a1),d1            ; lo
       move.l  (a1),d2              ; hi
       cmpa.w  #$8500,a1            ; plane A only
       bne.s   chk
       move.l  d2,d3
       sub.l   d1,d3
       cmpi.l  #(2E+2)<<16,d3       ; wide enough?
       blt.s   chk
       addi.l  #E<<16,d1
       subi.l  #E<<16,d2
chk    cmp.l   $24(a1),d1           ; X <= lo ?
       bge.s   low
       cmp.l   $24(a1),d2           ; X < hi ?
       bgt.s   ok
       tst.l   d0                   ; X >= hi: accept only a move to the left
       bmi.s   ok
       bra.s   rev
low    tst.l   d0                   ; X <= lo: accept only a move to the right
       bgt.s   ok
rev    sub.l   d0,$24(a1)           ; undo the move, zero the velocity (as the original)
       clr.l   $34(a1)
ok     rts
7b044  jsr     $5000                ; (displaced call)
       lea     $8500.w,a1
       ... same range test ...; if X <= lo+E: X = lo+E+1.0 ; if X >= hi-E: X = hi-E-1.0
       d1 = newX - X ; X = old X(+$2C) = newX
       move.l  d1,$8534.w ; clr.l $8530.w ; lea $8540.w,a1 ; jsr $25E0   ; plane B share
       $8564 += $8574 ; $856C = $8564 ; clear $8574/$8570/$8534
       jmp     $3F96                ; (displaced call) initial plane fill
```
The "accept a move towards the range" rule is a superset of the original test (the original
never starts outside its range), so with E-bounds inactive the behaviour is identical.

Verification (E=40, `qs_wide_verify.py stream --clamp`): camera X range becomes 41..Xmax−43
in every area with Xmax ≥ $C0 (e.g. Duckburg 41..4501, 512-px rooms 41..149), starting
positions are moved inside (Duckburg end area starts at 4503), 0 streaming errors in all 48
areas, loc 6.7/6.8 (320-px rooms) unchanged. Gameplay consequence: at area ends the camera
stops E px earlier, so Donald can walk up to E px closer to the screen edge (he stays visible
because the view is wider); spawns at area start are computed from the shifted camera.
Not part of the default patch set (`patches(E, camera_clamp=True)` / `apply(..., camera_clamp=True)`).

**Port (src/game/qs.cpp), October 2026 additions.** The clamp is part of the port's widescreen set. An area
whose camera range is narrower than 2E+2 but not 0 (the 512-px rooms at 21:9: range 192 < 218) holds the
camera at the centre, lo + range/2, in both code paths (per frame: `$7B044` sets X and clears the velocity;
area load: `$7B0E0` starts at the centre). The view then shows the whole room with (536-512)/2 = 12 px masked
on each side. The player clamp is world-based, so Donald still reaches both ends. The masks are computed from
the plane A scroll the frame is drawn with (`pre_frame`: the uploaded H scroll when reg 11 is full-screen,
else `$8FB4 - $8F86`). `$8F86` is a screen offset for shakes (final boss) and the Viking ship's sway, which
the game adds to every sprite and subtracts from the plane scroll.

## 5. HUD (status bar)

Window plane at VRAM `$D000` (reg 3 = $34), 64 cells per row, rows 24..27 (lines 192..223,
reg 17 = 0, reg 18 = $98), columns 0..39 used (40..63 zero), all tiles high priority. The four
fixed SAT entries (`$99A0`: X = 1 / X = 0 masking sprites at Y = $140) hide all sprites on
lines 192..223 — the port's HUD rendering for those lines must not composite sprites or
planes in the extension either.

| window cols | pixels | content |
|---|---|---|
| 0 | 0..7 | left border |
| 1..8 | 8..71 | Power (label row 24, 8 hearts row 26) |
| 9 | 72..79 | gap (bolts at x 75,76; box frame at x 72 and 79) |
| 10..14 | 80..119 | Temper |
| 15 | 120..127 | gap |
| 16..19 | 128..159 | Donald (lives, row 26 "03") |
| 20 | 160..167 | gap |
| 21..25 | 168..207 | weapon box (icon 21..22, ammo bar 23..25) |
| 26, 27..28, 29 | 208..239 | gap, item box, gap |
| 30..38 | 240..311 | Points (x 239 = box/strip edge) |
| 39 | 312..319 | right border |

Label strip (row 24) is dark from col 0 to 19 and 30 to 38 (plus a small dark strip over
cols 24..25), light panel in 20..23 and 26..29.
No 8-px column is "plain" in all four rows, but several pixel columns are: x = 73, 74, 77, 78
(dark label strip + plain panel) and x = 233, 237, 238 (plain panel on all rows). The remap is
therefore per output **pixel** column (same mapping for all 32 lines):

| output x (0..319+2E) | source x | part |
|---|---|---|
| 0 .. 78 | x | border + Power, anchored left |
| 79 .. 78+E | 78 | filler (continues the dark label strip) |
| 79+E .. 238+E | x − E | Temper, Donald, weapon, item box — centred |
| 239+E .. 238+2E | 237 | filler (plain panel) |
| 239+2E .. 319+2E | x − 2E | Points + right border, anchored right |

E=40: [0,79)←[0,79), [79,119)←78, [119,279)←[79,239), [279,319)←237, [319,400)←[239,320).
E=20: [0,79)←[0,79), [79,99)←78, [99,259)←[79,239), [259,279)←237, [279,360)←[239,320).
(`qs_wide_util.hud_src_x()` / `hud_remap()`; rendered check: seamless at both E, no stray frame
lines.) Cell-granular fallback if pixel remap is impossible: repeat window column 9 (bolts
every 8 px, acceptable) at the same insertion points.

Apply the remap when: gameplay condition of §6 holds and reg 18 = $98. The bar content is
dynamic (hearts, temper, lives, weapon icon/ammo, item, score) but the layout never moves.

Sprite-based HUD/overlays: none during gameplay (a scan of all 48 areas for objects with
+$0C bit 3 "screen coordinates" found none; no boss energy bars exist in this game). The only
screen-space sprite overlay is **GAME OVER** (state F): player slot `$FFA2C0` becomes type $5D
with flags $18, SAT slots 4..8, tiles $780..$79x palette 0 priority (word $8780..), drawn
centred at x 116..211 — it is centred on the 320 view and therefore also on the wide view, so
no shift is needed. The START menu (weapon/item select, "USE / LOOK / CALL THE AIRPLANE") is a
separate full-screen mode (§6).

## 6. Non-gameplay screens

State log from power-on (`$FFFF18`, `$FFFF1A`): SEGA logo ($8000, 0) → attract/"cast" cave
scene ($0001, 3) → title → menu START/OPTIONS ($800B, 5) → story text ($000C, 3/5) → world map
($0002, 0) → level load (4) → gameplay ($0005, 1).

| screen | state / VBlank mode / regs | at E=40 | decision |
|---|---|---|---|
| SEGA logo | $8000 / 0 | black sides | pillarbox |
| intro "cast" cave scroll, title | $0001 / 3, reg 11 = 2 (cell scroll), reg 16 = 3 (128x32 plane) | cave strip continues seamlessly over 400 px, texts centred | wide OK (pillarbox also fine) |
| START/OPTIONS menu, options | $800B / 5 | checkerboard only 320 wide, black sides | pillarbox |
| story text | $000C / 3 | black background | pillarbox |
| world map + plane flight | $0002 / 0, reg 18 = 0 | map picture is ~272 px inside the 320 area, sides black | pillarbox (identical look) |
| START menu (weapon/item/USE/LOOK/CALL THE AIRPLANE) | $0008 / 4 | menu drawn in the 320 area, extension shows the frozen level → wrong | **pillarbox** |
| game over | $800F / 2 (frozen level + GAME OVER sprites) | fine (level still valid) | wide OK |
| continue screen | $8010 / 1..2, reg 18 = 0 | black + centred box | pillarbox |
| Duckburg departure (plane over the city, state A) | $000A / 3 | city strip spans 400 px | wide OK |
| ending / Daisy scene (state 9) | $0009 / 3 | scenery strip spans 400 px, text centred | wide OK (pillarbox safer) |
| level load | $0004 | black | either |

**Condition for widescreen gameplay** (evaluate each frame):
`(word $FFFF18 & $7FFF) == 5 && word $FFFF1A == 1` (plus state $F / mode 2 for the game-over
freeze if desired). Everything else: pillarbox to 320 (optionally allow modes 3 with states
1/9/A, the scrolling cutscenes). The HUD remap (§5) applies only when the gameplay condition
holds and reg 18 = $98. The mask rule (§4a) applies only in gameplay.
Transitions (door/area change, death) go through state 4 with the screen already faded to
black, so switching between wide and pillarbox there is invisible; a port may also simply keep
the last decision while state = 4.

## 7. Special cases

| case | where | what happens at E=40 | action |
|---|---|---|---|
| Raster / HINT | — | none: reg 0 = $04 (IE1 off) from the init table at `$294` onward, the level-6 vector points at the `$200` idle loop, and no code writes reg 0 with bit 4 or reg 10 for line interrupts | nothing |
| Per-line H scroll (reg 11 = 3) | `$FFEE01` bit 0: Mexico areas 0..2 (`$EF48` = `$29B4`/`$29C4`/`$2A20`), South Pole area 2 (`$2CAC`) | plane-B parallax bands (clouds, mountains, water) built at `$FF8B00`, band list `$FF8D00` (4 x {flag<0 = plane A, first line, last line, table ptr}), table `$FFF700` built by `$27EE`, DMA'd to `$D000`. The bands show the whole 512-px plane row | handled by the clamp-at-0 in `stream_col`/`stream_row` (§2); verified with `check_displayed` + screenshots (`qs_wide_mexico_bands.png`, `qs_wide_southpole_ice.png`) |
| Cell H scroll (reg 11 = 2) | attract "cast" scene / title (VBlank mode 3 handler `$618`, flags `$FFFC06`/`$FFFC04`) | cave strip is valid across 400 px (plane 128 cells wide) | wide OK |
| Screen shake / sway offsets `$FF8F86` (X) / `$FF8F88` (Y) | Viking ship area 0 (`$2B9C`: ±3 px sine sway), used by VBlank scroll and sprite code | a few px of extra horizontal offset — well inside the ≥41 px streaming margin | nothing (`qs_wide_viking_ship.png`) |
| Plane B as a moving crusher | Transylvania area 4 (`$2B04`: plane B Y velocity `$8574`, player X clamp to planeA−planeB+$650), Egypt area 4 (`$2F32` state machine, plane B ceiling `$8570`, player Y clamp) | plane B moves on its own; plane B streaming follows its own X/Y | nothing (plane B checked in the pan test) |
| Direct nametable writes | Egypt area 4 wall (`$2F74` → `$41F6`) | see §2 | nothing |
| Vertical shafts | South Pole 3 (Ymax $420), Egypt 1 (Ymax $320), Pete's hideout 1..3 (Ymax $220), Island 0..1 | rows are always 512 px wide, so vertical scrolling needs no change | verified (`qs_wide_southpole_shaft.png`, `qs_wide_egypt_shaft.png`, pan test D/U/diagonal) |
| Run-time bound changes | `$3098`/`$3152` handlers set Y max (`$8508`) and player Y max (`$8F6C`) by player X | vertical only | nothing |
| Palette cycling | many handlers (`$F5xx` + `$FFEE02`) | — | nothing |
| Mine carts | Egypt 5 (rides left), hideout 1 | ordinary follow camera, objects (money bags) spawn on the left | covered by the spawn patches |
| Single-screen rooms | Maharajah 7/8 (Xmax = 0) | whole extension outside the room | runtime mask (§4a) = pillarbox (`qs_wide_room_320.png`) |
| 512-px rooms | Mexico 2, Transylvania 6, Viking 2/4, Maharajah 1/2, hideout 4, Island 3 | 192 px of camera travel | mask at the ends, or camera clamp (range 41..149) |
| START menu (state 8) | VBlank mode 4 | menu only covers 320 px; extension shows the frozen level | pillarbox (§6) |
| Status-bar lines | lines 192..223 | the core renders plane A/B (and nothing else) in the extension of the window lines; masking sprites at X=0 hide sprites only as far as the core implements the rule | port replaces those lines with the §5 remap |
| Object edge-parking | `$12160`, `$10F0C`… | parked objects partially visible | optional immediates (§3 table) |

Nothing else found that breaks at E=40. Sprite limits: the game never builds more than 76
pieces; per-line limits are the core's business (`sprite_limit=False` in all tests).

## 8. Patch summary

Default set (`qs_wide_patch.patches(E)`): `checksum`, `stream_col`, `stream_row`,
`stream_init`, `spr_xmin`, `spr_xmax`, `spawn_left`, `spawn_init`, `despawn`.
Optional (`camera_clamp=True`): `cam_hook_code`, `cam_bounds`, `cam_bounds2`, `cam_load`.
Only `spr_xmin`/`spr_xmax` (and the optional clamp immediates) depend on E.

E = 40:

| patch | addr | original | new |
|---|---|---|---|
| checksum | `$40A` | `6600 00F6` | `4E71 4E71` |
| stream_col | `$26C8` | `6D0E 3029 0024 3229 0020 4EBA 00B2 6010 3029 002C 0640 0200 3229 0020 4EBA 00A0` | `6D06 3029 0024 6008 3029 002C 0640 0200 0440 0060 6A02 7000 3229 0020 4EBA 00A0` |
| stream_row | `$26FC` | `6D0C 3029 0024 3229 0020 6000 0014 3029 0024 3229 0028 0641 0100 6000 0004` | `6D06 3229 0020 6008 3229 0028 0641 0100 3029 0024 0440 0060 6A02 7000 6002` |
| stream_init | `$3FCE` | `4EBA E74C` | `4EBA E73C` |
| spr_xmin | `$9BCE` | `0C40 0060` | `0C40 0038` |
| spr_xmax | `$9BD4` | `0C40 01C0` | `0C40 01E8` |
| spawn_left | `$19C1C` | `0440 0071` | `0440 00F1` |
| spawn_init | `$19D74` | `0C47 0280` | `0C47 0300` |
| despawn | `$9A9A` | `0C44 0280 6400 1C22 4A44 6B00 1C1C` | `3004 0640 0080 0C40 0300 6400 1C1C` |
| cam_hook_code (opt.) | `$7B000..$7B0CD` | 206 x `FF` | `2229 0004 2411 B2FC 8500 6618 2602 9681 0C83 0052 0000 6D0C 0681 0028 0000 0482 0028 0000 B2A9 0024 6C0C B4A9 0024 6E12 4A80 6B0E 6004 4A80 6E08 91A9 0024 42A9 0034 4E75 4EB9 0000 5000 43F8 8500 2229 0004 2411 2602 9681 0C83 0052 0000 6D00 0068 0681 0028 0000 0482 0028 0000 2029 0024 B081 6E0A 2001 0680 0001 0000 600E B082 6D00 0044 2002 0480 0001 0000 2200 92A9 0024 2340 0024 2340 002C 21C1 8534 42B8 8530 43F8 8540 4EB9 0000 25E0 2038 8574 D1B8 8564 21F8 8564 856C 42B8 8574 42B8 8570 42B8 8534 4EF9 0000 3F96` |
| cam_bounds (opt.) | `$2664` | `2229 0004 B2A9` | `4EB9 0007 B000` |
| cam_bounds2 (opt.) | `$266A` | `0024` | `6018` |
| cam_load (opt.) | `$35A0` | `4EBA 1A5E 4EBA 09F0` | `4EB9 0007 B044 4E71` |

E = 20: identical except `spr_xmin` → `0C40 004C`, `spr_xmax` → `0C40 01D4`, and in
`cam_hook_code` the immediates `0052 0000` → `002A 0000` ((2E+2)<<16) and `0028 0000` →
`0014 0000` (E<<16). General E: spr_xmin = $60−E, spr_xmax = $1C0+E (valid for E ≤ 64).

Instead of the `checksum` NOP the port may keep `$40A` and store the new sum at `$18E` (sum of
all words `$200..$7FFFF`, mod $10000; the patched ROMs above including the NOP sum to $8756
(default set) — recompute if the NOP is left out).

## 9. Port-side rules (summary)

1. Apply the default patch set at ROM load (optionally the camera clamp).
2. `wide = (word $FFFF18 & $7FFF) == 5 && word $FFFF1A == 1` (optionally also state $F /
   mode 2 = game over, and the mode-3 cutscenes). Otherwise pillarbox to the centre 320 px.
3. When `wide`: lines 0..191 masked per §4a (`$FF8FB4`, `$FF8504`, `$FF8500`); lines 192..223
   (if reg 18 = $98) = window plane remapped per §5 (no sprites, no planes in the extension).

## 10. Verification log (how to reproduce)

- `python tools/re/qs_wide_verify.py stream [--orig] [--clamp] [--E 20]` — 48 areas, full pans,
  per-frame nametable check. Results: patched E=40: 0 bad frames; E=40 + clamp: 0; E=20: 0;
  E=20 + clamp: 0 (camera range 21..Xmax−23);
  original E=40: 500..2600 bad frames per scrolling area.
- `python tools/re/qs_wide_verify.py sprites [--orig]`, `spawn [--orig]` — numbers in §3.
- `python tools/re/qs_wide_verify.py edges [--clamp]` — camera ranges and mask values in §4.
- `python tools/re/qs_wide_demo.py [outdir]` (default `build/qs_wide_demo/`) — 12 sections,
  BEFORE (original ROM, E=40) above AFTER (patched + port rules), red dots = original 320 view,
  frame labels include the automatic check (bad cells per frame: BEFORE 3..6, AFTER 0 in all
  scrolling sections). Visually checked: Duckburg walk right/left (left extension continues the
  wall instead of showing a building 512 px ahead; area start masked), Mexico bands (correct
  cacti/rocks, clouds seamless), Egypt/India (enemies entering from the right are complete in
  the extension), Transylvania pan left (no pop-in), South Pole and Egypt shafts (vertical +
  diagonal), Viking ship sway, South Pole ice (line scroll), 512/320-px rooms (masking), START
  menu (pillarbox, clean redraw on return).
- Same demo at E=20 (`--E 20 build/qs_wide_demo_e20`): BEFORE 2..3 bad cells per frame, AFTER 0;
  HUD remapped to 360 px, masks 0..20 px.
