# Castle of Illusion (USA) — widescreen research (E extra px each side)

Scripts: `tools/re/coi_wide_common.py` (helpers, patch tables, caves, validators, port rules),
`tools/re/coi_wide_demo.py` (before/after review strips → `tools/re/coi_wide_out/`, `…/e20/` for E=20),
`tools/re/coi_wide_verify.py` (reproduces the verification numbers below).
States: `tools/states/coi_wide_toy3.state` (round 07 rotating-room level, camera X≈1000),
`coi_wide_options.state` (options screen), `coi_wide_hub.state` (castle hall).

**Summary / recipe for the port (E = 20 or 40)**
1. Tile streaming needs **no patch**: the engine keeps the whole 512x512 plane consistent (§2).
2. Apply the ROM patches of the table below (sprites P1-P5, objects O1-O8, animated blocks A1-A2,
   optional SFX S1) and the camera clamp C1-C3; recompute the checksum word at `$18E` (patch list).
3. Per frame: `wide = ($F300 == $0E || $F300 == $10)`; otherwise pillarbox to 320 (§6).
4. When wide: black-mask `maskL/maskR` columns on lines 0-191 from `$FA84`, `$FAB0`, `$FAB2` (§4) — only
   non-zero in 4 boss arenas once C is applied; remap lines 192-223 (HUD) with the table of §5.
5. Renderer: apply X=0 sprite masking across the whole widened line, no 20-sprites/line limit (§7).

All addresses are ROM offsets / 68000 addresses; RAM `$FFxxxx` is written `$xxxx.w` in disassembly.

Notation: "screen x" = x in the original 320-px view (0..319); the widened view covers screen x in [-E, 320+E).
VDP sprite X = screen x + 128.

---------------------------------------------------------------------------------------------------

## 0. Game structure (what you need to navigate)

| RAM | meaning |
|---|---|
| `$F300.w` | main state (jump table at `$03D8`, word offsets). `$0C` = round load (`$95D2`), `$0E` = gameplay (`$97BA`), `$10` = rotating-room flip effect (`$FC50`), `$18` game over, `$24` end-of-round sequence; full list in §6 |
| `$F302.w` | VINT mode (jump table at `$D036`). 1 = gameplay, full-screen H scroll; 4 = gameplay, per-line H scroll (DMA `$FFEC00` → VRAM `$AC00`, 192 lines) |
| `$FF42.w` | round index (0..$1B). Next-round table at `$98BE` (0→2→3→1→4→5…) |
| `$FF44.w/$FF46.w` | world / sub-level used for the level header table `$E8F2` |
| `$F41D.b` | pause flag |
| `$F40F.b` | "player died" → reload `$FF42` (used by our level jump) |
| `$F412.b` | round cleared → `$FF42 = next[$FF42]` |
| `$F322.w` power, `$F324.w` tries, `$F446.b` hit-invulnerability timer, `$F459.b` skips instant-death blocks |
| `$DC00` | player object (`+$10` X.l 16.16, `+$14` Y.l, `+$18` Xvel, `+$1C` Yvel, `+$03` routine) |

Level jump used by all scripts (`coi_wide_common.goto_level`): from any gameplay state write `$FF42 = n`, `$F40F = 1`, run ~240 frames.

Round list (by look; names are approximate):

| $FF42 | content | camera X range | camera Y range | map (blocks WxH) |
|---|---|---|---|---|
| 00 | Enchanted Forest 1 | 0..3520 | 0..32 | 240x16 |
| 02 | Enchanted Forest 2 | 0..2240 | 0..544 | 160x48 |
| 03 | Forest, spider web | 0..1728 | 0..544 | 128x48 |
| 01 | Forest, tree climb | 0..2240 | 0..544 | 160x48 |
| 04 | Forest boss (tree) | **160..192** | 0..32 | 32x16 |
| 05 | Toyland 1 (vertical) | 0..448 | 0..2848 | 48x192 |
| 06 | Toyland 2 | 0..1472 | 0..288 | 112x32 |
| 07 | Toyland 3 (224-px strip, rotating room) | 0..4800 | 0 | 320x14 |
| 08 | Toyland boss | **192..192** | 0..48 | 32x17 |
| 09,0A | Storm / ruins 1,2 | 0..3776 | 0..544 | 256x48 |
| 0B | ruins 3 | 0..2240 | 0..32 | 160x16 |
| 0C | boss room (no object layout) | 0..192 | 0..288 | 32x32 |
| 0D..11 | water / library section (5 variants) | 0..960 | 0..1056 | 80x80 |
| 12..14 | Dessert door rooms | 0..704 | 0..32 | 64x16 |
| 15 | Dessert 1 | 0..3520 | 0..272 | 240x31 |
| 16 | Dessert boss (worm) | **3456..3520** | 0..272 | 240x31 |
| 17 | Castle 1 | 0..1728 | 0..1056 | 128x80 |
| 18 | Castle 2 (water) | 0..1728 | 0..288 | 128x32 |
| 19 | Clock tower 1 (vertical) | 0..704 | 0..1312 | 64x96 |
| 1A | Clock tower 2 / boss | 0..192 | 0..288 | 32x32 |
| 1B | Mizrabel | **192..192** | 0..288 | 32x32 |

---------------------------------------------------------------------------------------------------

## 1. Camera

Two identical `$40`-byte structs: **plane A / main camera at `$FFFA80`**, **plane B at `$FFFAC0`**.

| off | size | meaning |
|---|---|---|
| +$00 | w | camera Y (top of view), +$02 fraction |
| +$04 | w | camera X (left of the 320 view), +$06 fraction |
| +$08 | l | block map pointer (RAM, e.g. `$FF2000`), one byte per 16x16 block, row-major |
| +$0C | w | map height in blocks |
| +$0E | w | map width in blocks (row stride) |
| +$10 | l | block definitions (`$FF0000` for A, `$FF0800` for B): 8 bytes/block = TL,TR,BL,BR name-table words |
| +$14 / +$18 | w | previous Y / previous X (for the 16-px crossing tests) |
| +$1C / +$1E | w | dY / dX requested this frame |
| +$20 +$22 +$24 +$26 | w | min Y, **min X**, max Y, **max X** (camera clamp) |
| +$28 / +$2C | l | VDP command for the pending row / column |
| +$30..+$3D | | unused (only cleared at round load) — used by the optional clamp patch (§4) |
| +$3E / +$3F | b | row / column pending flags |

* H/V scroll write (VINT `$CF92`): VSRAM[0] = `$FA80`, VSRAM[1] = `$FAC0` (full-screen V scroll, reg 11 bit 2 = 0);
  H scroll table at VRAM `$AC00` (reg 13 = `$2B`): `$AC00` = -`$FA84`, `$AC02` = -`$FAC4`.
  In VINT mode 4 (reg 11 = 3, per-line) the whole 192-line table is DMA'd from RAM `$FFEC00` (`$D40E`);
  the table is built by the camera-type routines (e.g. `$F7F6`, `$F832`, `$DB2A`, `$D794`).
* Level header (`$E896`, table `$E8F2` indexed by `$FF44/$FF46`): `$9808` camera type, `$9802` parallax type,
  graphics id, `$9820.l` X dead-zone (right,left = `$C0`,`$80` screen px), `$9824.l` Y dead-zone (`$90`,`$38`),
  16 bytes camera A init, 16 bytes camera B init (Y.l, X.l (bit 31 = "lock min X to this X"), map ptr, H, W).
* Camera init `$EEAC` (a1 = struct): min X = 0 (or the init X when bit 31 set), min Y = 0,
  **max X = W*16 - 320**, max Y = H*16 - 224. The bounds are written **only here** (checked: no other writer
  of `$FAA0-$FAA7`/`$FAE0-$FAE7`; boss arenas are separate rounds whose map/bounds are narrow).
* Per frame (from the player object, `$AEFA`/`$AF4E` → `$F6E4`): `$F71A` clamps the player to
  [minX+16, W*16-16] x [8, H*16-16] and computes dX/dY = player screen position outside the dead-zone;
  `$F7A4` → `$F872` (plane A: X += dX, Y += dY, clamp to +$20..+$26, stream), `$FB10` (plane B deltas
  from plane A by parallax type `$9802`, table `$FB20`), then plane B either streams with the same
  `$F872` (`$9800` bit 0 clear) or just moves (bit 0 set: plane B is a 512x512 picture drawn once).
  Then the camera-type routine (`$9808`, table `$F6FE`) runs (palette cycling, per-line tables, …).
* Parallax types `$9802` (plane B from plane A): 0 none; 1 x/2, y*7/8; 2 x/2, y/2; 3 x/2, y/11; 4 x/2 (x only);
  5 B = A; 6 x*7/8, y; 7 x/2, y*2/3; 8 x/2, y/2; 9 special (`$FBD2`, wobbling table at `$FF4000`); 10 x*5/4, y;
  11 x/4, y*2/5.

## 2. Tile streaming

Planes are 64x64 cells (512x512 px), plane A at VRAM `$C000`, B at `$E000`, window `$B000`, SAT `$A800`, H scroll `$AC00`.
The engine keeps the **whole 512x512 plane consistent** with the block map, not just the view:

* Column (`$F982`): when bit 4 of X changes, write block column `X/16+24` (moving right, dX >= 0) or
  `(X+15)/16-8` (moving left); 32 blocks tall starting at block row `Y/16-7` → buffers `$FF1000` (A) / `$FF1200` (B).
* Row (`$F8CE`): when bit 4 of Y changes, write block row `Y/16+24` (down) or `(Y+15)/16-8` (up);
  **32 blocks wide** starting at block column `X/16-7` → buffers `$FF1100` (A) / `$FF1300` (B).
* VINT `$FA9A` flushes pending rows/columns (auto-increment `$80` for columns).
* Initial draw `$EDF0`: 33 columns from `X/16-8`.

In pixels: a new column is written 64..79 px beyond the right edge of the 320 view when moving right
(screen x 384..399) and 113..128 px beyond the left edge when moving left; rows 160..175 px below the
224-px view / 113..128 px above it; every row and column covers the full 512 px of the plane
(rows from X-112, columns from Y-112).

So after any frame the plane holds block columns `[X/16-8+k, X/16+23+k]` (k=0/1 by direction) and rows likewise.
The widened view needs columns `floor((X-E)/16) .. floor((X+319+E)/16)`; this is satisfied for every E <= 64
(right: `X/16+24 >= floor((X%16+334+E)/16)`; left: `ceil((15+E)/16) <= 8`).
Only precondition: camera moves < 16 px per frame (same as the original).

**No streaming patch is needed for E = 20 or E = 40.**

Verification (`coi_wide_common.check_plane`): for every frame, every 8x8 cell visible in the widened view
(lines 0..191, both planes when plane B streams) is compared with the name-table word the RAM block map
predicts (uses the real per-line H scroll values). The camera was driven directly (test-only NOP of the
`jsr $F71A` at `$F6E4`, dX/dY written each frame) in a serpentine over the full camera range of all 28 rounds:
* E=40, 6 px/frame, all rounds: 0 mismatching cells in the extensions (and 0 in the centre) —
  ~55 000 frames checked. Only "outside" cells (map coordinates < 0 or >= W*16) at level edges, see §4.
* E=64, 12 px/frame, rounds 00/05/09/0D/19: 0 mismatches.
* E=40, 6 px/frame, all rounds, **with all patches + clamp C**: 0 mismatching cells, and plane A never shows
  a cell outside the map except in the 4 locked arenas (covered by the mask) — `coi_wide_verify.py planes 40 clamp`.
  (This run is what caught the naive animated-block widening and the plane-B start jump, both fixed.)
* E=20 and E=40, `coi_wide_demo.py` sweep of rounds 00/05/06/09/0D/17/19 with patches: 0 bad frames.
(Flying the *player* through walls produces camera jumps of 16-32 px and missing rows/columns in the centre
as well; that is an artefact of noclip, not of the widescreen.)

### Animated map blocks (`$9400` table, 32 x `$20`)
`$F0C0` animates blocks by rewriting both the RAM map and the name table (`$F224`, immediate VDP writes;
entry `+$18/+$1A` block X/Y, `+$1C/+$1E` width/height in blocks, `+$14` cached VRAM address).
Entries with flag bit 0 are only processed when visible: `$F32E` tests `X+W >= camX/16` and
`X <= camX/16 + 20` (blocks) — the 320-px view. Outside, the animation (timer and tiles) is frozen.
Used in Toyland 1 (13 entries, 1x2 blocks) and Toyland 2 (17 entries, up to 5x4 blocks).
At E=40 blocks in the extensions stay frozen (verified: entry 4 of round 06 at screen x -44..-29 never
changes without the patch; animates, map bytes 159→164, with it).

A plain "+k blocks" widening is **wrong**: `$F224` writes all W blocks of an entry, wrapping at 32 block
columns, while the streamer only keeps block columns `[cx-7, cx+24]` valid (cx = camX/16, both scroll
directions, after the crossing frame). The original test never leaves that range for W <= 5 (start >= cx-W,
end <= cx+24). With W=5 and +3 blocks the animation overwrote live columns on the opposite side of the plane
(seen as garbage in the right extension, e.g. a "?"-block tile at screen x 338..359 in Toyland 2, and at
x -40..-1 when scrolling left; a first cave version that allowed start = cx-8 still failed 23 frames of the
sweep). The patch therefore replaces the test with CAVE_ANIM:
visible ⇔ `X+W+k >= cx` ∧ `X+7 >= cx` ∧ `X <= cx+20+k` ∧ `X+W-25 <= cx`, k = ceil(E/16).
Wide entries (W=5) thus still start animating only when their right end reaches `cx+24` (up to 3 blocks
of such an entry can be frozen while in the right extension); narrow ones get the full widening.
Consequence: those blocks start animating up to k blocks earlier (their phase when entering the old view differs).

## 3. Objects / sprites

* Object slots: `$D000`-`$DBFF` (48 x `$40`), player `$DC00`, `$DC40`-`$DFFF` (15). Object: `+0` id.w,
  `+2` flags (bit0 delete, bit2 hidden, bit3 screen coordinates, bit4 never delete by position, bit7 high
  priority list), `+$10` X.l, `+$14` Y.l, `+$24` pointer to its layout entry's "spawned" byte.
* Run + display lists `$187C`; sprite builder `$1AB6`:
  * `d4 = X - camX + $80`, `d3 = Y - camY + $80` (unless flag bit 3).
  * **Delete window** `$1B12`: unless flag bit 4, delete (`$1C94`, clears the layout "spawned" byte so it can
    respawn) when `d4 ∉ [0,$280)` or `d3 ∉ [0,$280)` → X-camX ∈ [-128, 512).
  * Per sprite piece: Y test `$1C1E`: VDP Y in [$60,$180); **X test `$1C34`: VDP X in [$60,$1E0)** (screen
    x -32..351) unless object `+$E` bit 5. Culled pieces are simply skipped.
  * Sprite budget `$F402` = 73 pieces (SAT in RAM `$F500`, DMA'd to `$A800` in VINT).
* **Spawner** `$6D52A` (every frame), layout decompressed at `$CA00`: word offset to list table, byte grid
  width/height (128x128-px cells), grid of list indices, lists of 9-byte entries
  (flags, id, sub, spawned, -, Y.w, X.w). Flag bits 7/6 choose slot group (`$D000` 32 / `$DD80` 10 / `$D400` 32).
  * Horizontal: when bit 7 of camX changes, spawn the cell column containing `X+$180` (moving right) or
    `X-$71` (moving left), cell rows `Y/128-1 .. Y/128+3`.
  * Vertical `$6D550`: when bit 7 of camY changes, spawn row `Y+$180` / `Y-$71`, columns `X/128-1..X/128+3`.
  * Initial fill `$6D768`: columns at `X-$71 .. X+$18F`.
  * Many layout entries are deliberately stored in a cell *right* of their position (dx up to -352 px), so
    they appear when the camera reaches a later cell (these pop in inside the view in the original too).
* Moving right, normal objects therefore appear at screen x 384+dx (≈ 350..383 for objects stored up to
  34 px left of their cell) → **visible pop-in in [320,360)**. Moving left, the cell `X-$71` is spawned
  when its objects are already at screen x -127..0 → **pop-in in [-40,0)**. Both are fixed by shifting the
  spawner's notion of camera X by ±E (patch O2/O3: the 128-px crossing test and the look-ahead use
  camX+E when dX >= 0, camX-E when dX < 0): every object appears relative to the widened edge exactly where
  it appeared relative to the old edge. The row spawner (vertical scrolling, O4-O6) then has to cover the
  cell columns of both shifted variants: 6 columns from `(camX-E)/128-1`; the initial fill (O7/O8) covers
  `camX-E-$71 .. camX+E+$18F`. The delete window (O1) is widened by E on both sides so objects spawned
  E px earlier are not deleted immediately (first version got this wrong and lost objects — caught by the
  pop-in test).
* `$1FF6` "object on screen" (camX < X <= camX+320, camY < Y <= camY+192) is only used to decide whether
  to play an enemy SFX (4 callers) — widened by patch S1 (optional).
* Objects `$BE/$BF` (Dessert 1 background sprites that move with plane B, `$FDFC/$FE0C`): own window
  `$FE8E/$FE94` ([$60,$1E0)) and appear test `$FEB6` — patched with the sprite group.

Pop-in measurement (camera driven 3 px/frame right → left → right at the start Y; a "spawn" is an object
slot receiving a new layout pointer; counted when the spawn position is >= 8 px inside the view, lines 0..191):

| round | inside 320 view, original (E=0) | inside 400 view, **unpatched**, E=40 | inside 400 view, **patched**, E=40 | inside 360 view, patched, E=20 |
|---|---|---|---|---|
| 00 Forest 1 | 1 | 10 | 1 | 1 |
| 02 Forest 2 | 0 | 2 | 0 | |
| 06 Toyland 2 | 1 | 1 | 1 | |
| 09 Storm 1 | 4 | 20 | 4 | 4 |
| 0E Library 2b | 1 | 3 | 0 | |
| 15 Dessert 1 | 2 | 10 | 2 | 2 |
| 17 Castle 1 | 1 | 6 | 1 | 1 |
| 18 Castle 2 | 2 | 5 | 4 (*) | 2 |
| 19 Clock tower 1 | 2 | 4 | 2 | |

The remaining "inside" spawns are the ones the original also shows inside its 320 view (objects stored in a
cell far from their position on purpose: things that drop in, etc.).
Static classification of all 1340 layout entries (`t24`): moving right, 54 entries pop in in the right
extension without the patch (all fixed); moving left, 367 entries pop in in the left extension (all fixed).
(*) Known limitation: 11 entries are *trailing* objects the designers spawn just behind the player
(rounds 06/07 ids $32/$33, 0B $3F, 18 $44 at screen x -48..-16 when moving right; 0A $3F when moving left).
With the shifted trigger they appear E px further in (e.g. $44 in Castle 2 appears at x=+7 instead of -32).
At E=40 they are visible in either case; at E=20 the unpatched spawn would have been hidden. A per-object
filter in `$6D612` would be needed to handle them; judged not worth it.

Gameplay consequences of the object patches: objects are spawned (and start their AI) E px earlier in the
direction of travel and are deleted E px later; positions/AI/collision are untouched. Side effect: the
three attract-mode demos (rounds 06, 19, 02, recorded input) drift slightly — with the object patches demos
2 and 3 take one extra hit; with the clamp patch too, demo 1 ends in a different place. Harmless (demos end
on a timer), but a port that wants bit-exact demos would have to disable the patches while `$F40A` (demo) is set. Slot pressure checked:
spawn failures because all 32 `$D000` slots are busy (`$6D67A`) are identical with and without patches
(Storm 1: 1/1, Castle 1: 0/0, Forest 1: 0/0).

### VDP sprite X range
Culled pieces keep VDP X in [$60-E, max($1E0,$1C0+E)) = [$38,$1E8) for E=40, [$4C,$1E0) for E=20:
screen x -72..359 / -52..351, inside the renderer's -128..383. Pieces are at most 32 px wide.

## Patch list (all verified to assemble; `coi_wide_common.patches(E)`)

Words are big-endian 16-bit. `E` = extension, `k = ceil(E/16)`, `up = max($1E0, $1C0+E)`.

| id | addr | original | new (E) | E=40 | E=20 | purpose |
|---|---|---|---|---|---|---|
| P1 | `$1C3C` | `0C40 0060` | `0C40 $60-E` | `0C40 0038` | `0C40 004C` | piece cull left |
| P2 | `$1C42` | `0C40 01E0` | `0C40 up` | `0C40 01E8` | `0C40 01E0` | piece cull right |
| P3 | `$FE8E` | `0C41 0060` | `0C41 $60-E` | `0C41 0038` | `0C41 004C` | plane-B sprites hide left |
| P4 | `$FE94` | `0C41 01E0` | `0C41 up` | `0C41 01E8` | `0C41 01E0` | plane-B sprites hide right |
| P5 | `$FEBA` | `0641 0018` | `0641 $18+E` | `0641 0040` | `0641 002C` | plane-B sprites appear left |
| O1 | `$1B12` | `0C44 0280 6400 017C 4A44 6B00 0176` | `3004 0640 E 0C40 $280+2E 6400 0176` | `3004 0640 0028 0C40 02D0 6400 0176` | `3004 0640 0014 0C40 02A8 6400 0176` | delete window: `move.w d4,d0; addi.w #E,d0; cmpi.w #$280+2E,d0; bcc.w $1C94` → X-camX in [-128-E, 512+E) |
| O2 | `$6D53C` | `302E 0004 362E 0018` | `4EB9 0007 FE80 4E71` | same | same | jsr CAVE_H |
| O3 | `$7FE80` | `FFFF`x13 | CAVE_H (below) | | | shifted camera X for the column trigger |
| O4 | `$6D586` | `302E 0004 0240 FF80` | `4EB9 0007 FE9A 4E71` | same | same | jsr CAVE_V |
| O5 | `$7FE9A` | `FFFF`x7 | CAVE_V | | | row spawn from camX-E |
| O6 | `$6D59C` | `7E04` | `7E05` | | | row spawn 6 columns |
| O7 | `$6D772` | `7E00` | `7E(-E)` | `7ED8` | `7EEC` | initial fill from camX-E |
| O8 | `$6D786` | `0C47 0280` | `0C47 $280+E` | `0C47 02A8` | `0C47 0294` | initial fill to camX+E |
| A1 | `$F338` | `3601 D668 001C B642 6500 002C 0642 0014 B242 6222` | `4EB9 0007 FF40 6500 002E 4E71 4E71 4E71 4E71 4E71` | same | same | jsr CAVE_ANIM; bcs.w $F36E |
| A2 | `$7FF40` | `FFFF`x26 | CAVE_ANIM (below, k=ceil(E/16)) | | | anim visibility test |
| S1 (on by default in the scripts) | `$200C` | `2238 FA84 B2A8 0010 6410 0681 0140 0000 B2A8 0010 6504` | `2228 0010 92B8 FA84 0681 E 0000 0C81 320+2E 0000 6404` | `…0681 0028 0000 0C81 0190 0000 6404` | `…0681 0014 0000 0C81 0168 0000 6404` | SFX on-screen test (optional) |
| C1 | `$EEEC` | `3029 000E E948` | `4EF9 0007 FEB0` | same | same | camera init: jmp CAVE_CAM (§4) |
| C2 | `$7FEB0` | `FFFF`x71 | CAVE_CAM (below) | | | E-adjusted bounds, plane B re-sync |
| C3 | `$F71E` | `3238 FAA2` | `3238 FAB0` | same | same | player clamp uses the saved original minX |
| – | `$18E` | `4270` | new checksum | `113B` (all incl. C) | `0E76` (all incl. C) | header checksum (without C: E=40 `955C`, E=20 `92BF`) |

```
CAVE_H ($7FE80)                         CAVE_V ($7FE9A)
 323C EEEE   move.w #E,d1                302E 0004  move.w 4(a6),d0
 4A6E 001E   tst.w  $1e(a6)   ; dX       0440 EEEE  subi.w #E,d0
 6A02        bpl.s  +2                   0240 FF80  andi.w #$ff80,d0
 4441        neg.w  d1                   4E75       rts
 302E 0004   move.w 4(a6),d0  ; X
 D041        add.w  d1,d0
 362E 0018   move.w $18(a6),d3 ; prev X
 D641        add.w  d1,d3
 4E75        rts
```
```
CAVE_ANIM ($7FF40)  in: d1 = X (blocks), d2 = camX/16, a0 = entry; out: carry set = invisible
 3601        move.w d1,d3
 D668 001C   add.w  $1c(a0),d3      ; X+W
 3003        move.w d3,d0
 5640        addq.w #k,d0           ; = $5040|(k<<9): k=3 5640, k=2 5440
 B042        cmp.w  d2,d0
 6520        bcs.s  inv
 3001        move.w d1,d0
 5E40        addq.w #7,d0
 B042        cmp.w  d2,d0
 6D18        blt.s  inv
 0443 0019   subi.w #25,d3
 B642        cmp.w  d2,d3
 6E10        bgt.s  inv
 3002        move.w d2,d0
 0640 0014+k addi.w #20+k,d0
 B240        cmp.w  d0,d1
 6206        bhi.s  inv
 023C 00FE   andi.b #$fe,ccr
 4E75        rts
 003C 0001   inv: ori.b #1,ccr
 4E75        rts
```
```
CAVE_CAM ($7FEB0)  entered by jmp from $EEEC, a1 = camera struct ($FA80 or $FAC0)
 3029 000E   move.w $e(a1),d0          ; W (the replaced instructions ...)
 E948        lsl.w  #4,d0
 0440 0140   subi.w #$140,d0
 3340 0026   move.w d0,$26(a1)         ; maxX (... as the original)
 3340 0032   move.w d0,$32(a1)         ; save original maxX  ($FAB2)
 3229 0022   move.w $22(a1),d1
 3341 0030   move.w d1,$30(a1)         ; save original minX  ($FAB0)
 B2FC FA80   cmpa.w #$fa80,a1
 663A        bne.s  planeB
 0641 EEEE   addi.w #E,d1
 0440 EEEE   subi.w #E,d0
 B240        cmp.w  d0,d1
 6F0C        ble.s  store
 3229 0030   move.w $30(a1),d1         ; range too small: centre
 D269 0032   add.w  $32(a1),d1
 E241        asr.w  #1,d1
 3001        move.w d1,d0
 3341 0022   store: move.w d1,$22(a1)
 3340 0026   move.w d0,$26(a1)
 3429 0004   move.w 4(a1),d2           ; pull the initial X into [minX,maxX]
 B441        cmp.w  d1,d2
 6C02        bge.s  +2
 3401        move.w d1,d2
 B440        cmp.w  d0,d2
 6F02        ble.s  +2
 3400        move.w d0,d2
 3342 0004   move.w d2,4(a1)
 3342 0018   move.w d2,$18(a1)         ; previous X
 4E75        rts
 0C78 0009 9802  planeB: cmpi.w #9,$9802.w   ; parallax type 9 has side effects: skip
 672A        beq.s  done
 2F0E        move.l a6,-(a7)
 4DF8 FAC0   lea    $fac0.w,a6
 4EB9 0000 FB10  jsr $fb10                   ; parallax deltas for B from the (moved) A
 302E 001E   move.w $1e(a6),d0
 D16E 0004   add.w  d0,4(a6)
 302E 001C   move.w $1c(a6),d0
 D156        add.w  d0,(a6)
 3D6E 0004 0018  move.w 4(a6),$18(a6)
 3D56 0014   move.w (a6),$14(a6)
 42AE 001C   clr.l  $1c(a6)
 2C5F        move.l (a7)+,a6
 4E75        done: rts
```
Code caves use the `$FF` fill at `$7FE6C-$7FFFF` (last code ends at `$7FE6A`; no references into the area):
CAVE_H `$7FE80-$7FE99`, CAVE_V `$7FE9A-$7FEA7`, CAVE_CAM `$7FEB0-$7FF3D`, CAVE_ANIM `$7FF40-$7FF73`.
The ROM checksum (`$18E`, verified by the boot code at `$31A`-`$342`, skipped only on soft reset) must be
recomputed after patching: 16-bit sum of all big-endian words from `$200` to the end
(`coi_wide_common.rom_checksum`; `patched_rom(E, clamp=True)` builds the complete patched image;
`apply_patches(md, ...)` also fixes it). All cave code was checked by disassembling the generated bytes.

## 4. Level edges / locked camera

The camera X range is fixed per round by `$EEAC` (min 0 or the header's locked X, max W*16-320) and never
changed at run time: boss arenas are separate rounds with narrow maps (04, 08, 16, 1B), there is **no
auto-scroll** (camera dX is only produced by the player follow code `$F79A`), and no script touches the
bounds. Everything the original can show is therefore `[minX, maxX+320)`.
Without any patch the widened view shows up to E px outside this range at every level start/end
(plane A reads the map outside its row → the neighbouring row's blocks = visible garbage, see
`coi_wide_out/coi_wide_5_edges.png`: forest start, Toyland 2, Clock tower, boss room 0C…).

### Runtime mask rule (renderer; always valid)
```
camX = s16(RAM $FFFA84)
lo, hi = s16($FFFAB0), s16($FFFAB2)   if the clamp patch below is applied (saved original bounds)
lo, hi = s16($FFFAA2), s16($FFFAA6)   otherwise
maskL = clamp(lo + E - camX, 0, E)    # black columns at the left of the widened view
maskR = clamp(camX + E - hi, 0, E)    # black columns at the right
```
Apply to lines 0..191 only (the HUD rows 192..223 are remapped, §5), and only in gameplay (§6 condition).

### Optional patch C: clamp the camera E px inside (recommended)
Hook `$EEEC` (inside `$EEAC`, replaces `move.w $e(a1),d0; lsl.w #4,d0` = `3029 000E E948`) with
`4EF9 0007 FEB0` (jmp CAVE_CAM). CAVE_CAM does the replaced work (maxX = W*16-320), saves the original
bounds at struct `+$30/+$32` (`$FAB0/$FAB2`; unused bytes, verified with a write watch over all rounds),
then for plane A only: minX += E, maxX -= E; if minX > maxX both become (minX+maxX)/2; the initial X/prev X
are pulled into the new range. For plane B it re-runs the parallax routine `$FB10` once so B starts at the
position that matches the moved A (otherwise the first frame jumps >16 px and a streamed column is lost —
seen as 16-31 bad frames in rounds 0E and 17 before this was added). Plane B bounds are left alone.
Plus `$F71E`: `3238 FAA2` → `3238 FAB0` so the player's own clamp (`$F71A`, minX+16) keeps using the
original minX (otherwise Mickey could not reach the first E px of a level / would stop E px early in arenas).

Result per situation (E=40; sweep `t26`, all rounds, all patches + C, camera over the full range,
0 mismatching cells anywhere):

| situation | rounds | with patch C | mask needed |
|---|---|---|---|
| level start / end (normal) | all except below | camera range [E, maxX-E]; view stays inside the level | none |
| boss arena, range < 2E | 04 (160..192), 08 (192), 16 (3456..3520), 1B (192) | camera locked at the centre (176 / 192 / 3488 / 192) | E=40: 04 24+24 px, 08 40+40, 16 8+8, 1B 40+40; E=20: 04 4+4, 08 20+20, 16 0, 1B 20+20 |
| narrow rooms (512 px wide) | 0C, 1A | range [40,152] | none |
| vertical shafts | 05 (768 px wide), 19 (1024 px) | range [40,408] / [40,664] | none |
| auto-scroll | none exist | – | – |
| rotating room | 07 (flip, §7) | – | none |

Plane B in streaming mode with parallax slower than plane A (rounds 17/18: B.x = 7/8 A.x; 19/1A: B.x = A.x/4)
can still show up to 5 px (17/18) or 30 px (19/1A) of plane-B cells outside B's own map at the extreme left
(and 14 px right in 19) — these read the neighbouring map row and look like plausible background
(checked on screenshots of rounds 17, 19, 1A at both ends). `camera_clamp_patches(E, plane_b=True)` would also clamp B, at the price of the
parallax stopping near the edges; not recommended.

Gameplay consequence of patch C: at the start of a level the 320-px "logical" view sits E px to the right of
where it was (Mickey appears E px further left in the old view, same place in the wide view); objects
spawn relative to the camera, so the first cell columns spawn E px earlier. In the four locked arenas the
camera does not move at all (previously 32/64 px of travel in 04/16).

## 5. HUD (window plane, lines 192-223)

Window plane at VRAM `$B000`, reg 17 = 0, reg 18 = `$98` (rows 24..27 = lines 192..223), 40 columns used,
columns 40..63 are 0. Written by `$9B4A` (dynamic parts: power `$BD04`, tries `$BD1E`, items `$BD30`, score `$BD3C`).

| rows | columns (cells) | content |
|---|---|---|
| 24 / 27 | 0..39 | border, alternating `$8078/$8079` (row 27: `$9078/$9079`, V-flipped) — 16-px period |
| 25-26 | 0-1 | blank `$800F` |
| 25-26 | 2-7 | POWER label / 5 stars (`$807C` full, `$807D` empty) |
| 25-26 | 8-11 | blank |
| 25-26 | 12-18 | TRIES icon (12-13), label / "x03" |
| 25-26 | 19-20 | blank |
| 25-26 | 21-28 | ITEMS icon (21-22), label / "x00" |
| 25-26 | 29 | blank |
| 25-26 | 30-37 | SCORE label / 8 digits |
| 25-26 | 38-39 | blank |

Reference implementation: `coi_wide_common.hud_src_x(out_x, line, E)` / `hud_remap(img, E)`; result for
rounds 00, 15, 19 at E=40 and E=20 in `coi_wide_out/coi_wide_6_hud.png` (border continuous, groups placed
as below; the ITEMS icon differs per world but occupies the same columns).
The layout is identical in every round (verified in rounds 00, 15, 19 and the forced ending scene); the bar
is never changed except through `$9B4A`. In the rotating room the window is not affected by the flip.

Remap (output x in 0..319+2E → source x in 0..319):
* lines 192-199 and 216-223 (border rows): `src = 64 + ((out - E) mod 16)` (continuous pattern).
* lines 200-215: POWER anchored left, TRIES+ITEMS centred, SCORE anchored right, filler from blank cols 8-11:

| out range E=40 | out range E=20 | source |
|---|---|---|
| 0..63 | 0..63 | `src = out` (margin + POWER) |
| 64..135 | 64..115 | filler `src = 64 + ((out-64) mod 32)` |
| 136..271 | 116..251 | `src = out - E` (TRIES + ITEMS, x 96..231) |
| 272..319 | 252..279 | filler `src = 64 + ((out-272) mod 32)` (E=20: `out-252`) |
| 320..399 | 280..359 | `src = out - 2E` (SCORE + right margin, x 240..319) |

Sprite overlays: SAT slots 0-3 are fixed masking sprites written by `$1A50` from `$1A76` every gameplay
frame (Y=$140/$160, 1x4, tile `$C438`, X=$40 then X=0 → sprite masking on lines 192-255 so no object sprite
overlaps the bar). The renderer must apply X=0 masking to the whole widened line.
"PAUSE" (`$F41D` set): slots 4-5 from `$1AA6`: Y=$D0, tiles `$80BD` (4x1) at X=$108 and `$80C1` (1x1) at
X=$128 — centred on the 320 view, which is also the centre of the widened view: no shift needed.
There is no boss energy bar and no other screen-fixed overlay: a scan of all objects in every round
(flag bit 3 = screen coordinates, visible) found only object `$93` in rounds 15/16 at VDP X = 64 and 0,
Y = 286/297 — another masking pair (hides object sprites on lines 158+ under the cream), handled by
full-line masking — and the plane-B sprites `$BE/$BF` (§3). Score pop-ups, collected items etc. are world
objects and scroll with the camera.

## 6. Non-gameplay screens

Main states (`$F300`, table `$03D8`) with VINT mode (`$F302`), as observed at E=40 (booting and pressing
START, or forcing `$F300` from a gameplay state):

| `$F300` | screen | `$F302` | at E=40 | show as |
|---|---|---|---|---|
| 00/02/04 | reset, SEGA logo | 0 | black sides, fine | pillarbox (harmless either way) |
| 06 | intro story ("ONCE UPON A MOUSE…", framed 256-px pictures + text), ends on the **title** logo + PRESS START | 2 | picture/logo planes repeat outside their frame | **pillarbox** |
| 08/0A | options (DIFFICULTY / SOUND TEST / CONTROL), reached with START from the intro/title | 0 | black sides | pillarbox |
| 28/2A | pre-game cutscene (Mizrabel, "HURRY! BEFORE MIZRABEL…") | 2 | framed picture + repeated content | **pillarbox** |
| 20/22 | castle hall hub (doors, "WHAT KIND OF ROOM IS BEHIND THIS DOOR?") | 5 | hall is a 512-px repeating picture scrolled by H scroll only (no streaming), looks right; its window text bar fills all 64 columns | wide is OK (no HUD remap), pillarbox is the safe choice |
| 0C | round load | – | display off | – |
| **0E** | **gameplay** | 1 / 4 | correct with the patches | **widescreen + HUD remap + edge mask** |
| **10** | **rotating-room flip effect** (Toyland 3) | 6 | correct (full-line VSRAM effect) | **widescreen + HUD remap** |
| 24/26 | gem received box after a boss (framed 256-px box) | 0 / 1 | box + black, fine | pillarbox |
| 18/1A | game over / continue | 0 | fine (black) | pillarbox |
| 2C/2E | ending scene after Mizrabel (set by `$10ECA`), per-line parallax `$DB2A`, HUD shown | 4 | looked consistent when forced from a gameplay state (not verified in the real flow) | pillarbox (safe) |
| 14/16 | ending castle fly-over | 1→2 | – | pillarbox |
| 12/1C/1E | copyright / credits, back to demo | 2 | – | pillarbox |

Flow: 00 → 02 SEGA → 04/06 intro+title → (START) 08/0A options → (START) 28/2A cutscene → 20/22 hall →
0C/0E rounds …; attract loop: 06 → 1C/1E copyright → 0C/0E demo (`$F40A` set) → 00. Round clear →
24/26 (gem box) → hall; death → 0C; no tries left → 18/1A.
Screens: `coi_wide_out/coi_wide_8_screens.png` (before raw / after pillarboxed).

**Condition** (evaluate once per frame, after the frame): `wide = ($F300 == $0E || $F300 == $10)`.
HUD remap (§5) only when `wide` and VDP reg 18 == `$98` (window = lines 192..223).
Mask rule (§4) only when `wide` (and not needed during `$10`: bounds/camera unchanged).
There are no act-intro cards or map screens in this game (the castle hall plays that role).

## 7. Special cases

* **Per-line H scroll** (reg 11 = 3, VINT mode 4: rounds 01, 09, 0A, 0C, 12-16, 1B; tables built per frame
  by the camera-type routines, e.g. `$F7F6`/`$F832` water ripple when `$9870` = 3/2, `$DB2A` bands,
  `$D794`, `$D9C6`): values are per line, so the extension just uses the same line offset; the plane
  validator used the real per-line values and found no problem. Nothing to patch.
* **Rotating room** (round 07, switch object `$07A0` sets `$F442` → camera type routine `$D6EC` sets
  `$F300 = $10`, `$FC50`): HINT `$D4C6` rewrites VSRAM every line from `$FF9000` (squash/flip), `$9801`
  toggles the upside-down mode (sprites Y-mirrored in `$1C56`, columns drawn reversed in `$FA52`, block
  writes in `$D66C`). Full-line effect, correct at E=40 (`coi_wide_out/coi_wide_7_flip.png`). Save state `tools/states/coi_wide_toy3.state`
  (round 07 at camera X≈1000; write `$F442=1` to trigger).
* **Plane B 512-px pictures** (`$9800` bit 0: most rounds): drawn once, wrap every 512 px — always valid.
* **Plane B streamed with slower parallax** (rounds 17-1A): up to 5/30 px of B outside its map at the
  extreme edges (§4).
* **HUD sprite masking**: slots 0-3 are X=0 masking sprites for lines 192-255 (§5) — the renderer must
  apply sprite masking to the whole widened line or object sprites will appear over the bar extension.
* **Sprite budget** `$F402` = 73 pieces/frame: minimum remaining while scrolling busy rounds with all patches
  (09, 15, 17, 00, 06): 41, 23, 36, 45, 57 — never reached. Disable the VDP 20-sprites-per-line limit in the
  port (more sprites share a line in 400 px).
* **Animated map blocks** (Toyland): see §2 — the naive widening corrupts the plane, use CAVE_ANIM.
* **Trailing objects** spawned just behind the player (11 layout entries): see §3.
* **Vertical wobble** in the boss room 0C (camera type 6, `$D794`): VSRAM goes to -1/-2 at camY = 0, so the
  top line shows one row above the map — identical in the original 320 view, not a widescreen issue.
* **Camera speed**: all streaming assumes < 16 px/frame (true in normal play). Teleport-style camera moves
  (e.g. debug level warps) leave stale rows/columns exactly like the original.
* **Hub pan** (`$F300 = $22`): camera X starts at -192 and pans; only H scroll, no streaming.

## 8. Verification summary (all reproducible)

| check | how | result |
|---|---|---|
| streaming, all 28 rounds, unpatched, E=40 | camera-driven serpentine 6 px/frame, `check_plane` on every frame | 0 wrong cells in extensions and centre (~55 000 frames) |
| streaming, E=64, 12 px/frame | rounds 00/05/09/0D/19 | 0 wrong cells |
| everything patched + clamp C, E=40 | `coi_wide_verify.py planes 40 clamp` (6 px/frame, every frame) | 0 wrong cells in all rounds; plane A outside the map only in the 4 locked arenas (masked); plane B outside its map in 17/18/19/1A (§4) |
| same, E=20 | `coi_wide_verify.py planes 20 clamp` | 0 wrong cells in all rounds; plane A outside the map only in 04/08/1B (16 no longer needs a mask); plane B outside its map in 17-1A |
| object pop-in | `coi_wide_verify.py objects 40` | table in §3 |
| sprite budget | `coi_wide_verify.py budget` | never reached (§7) |
| mask rule vs original visible range | per-4-px samples over camera sweeps (rounds 00/04/05/08/0C/16/1B, with and without C) | masked columns == columns outside `[minX, maxX+320)` exactly |
| real input | `coi_wide_demo.py` strip 1: Forest 1 run/jump right and back left, before and after in lockstep | identical camera path, clean edges, HUD remapped |
| screenshots | `tools/re/coi_wide_out/*.png` (E=40) and `e20/` (E=20) | reviewed: level starts, arenas, Toyland anim blocks, Dessert/Castle object entry, HUD, flip, menus |
| checksum | `rom_checksum` reproduces the header value `$4270` of the original ROM | patched images boot after `md.reset()` |
| attract demos | boot, let the three demos play with/without patches | drift slightly (§3) |
