# World of Illusion (USA) — widescreen reverse-engineering notes

ROM: `World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin`, 1 MB, header checksum
$2614 (verified at boot → patch `checksum`). Widescreen = the VDP renders E extra pixels on each side
(E=20 → 360×224, E=40 → 400×224).

**Summary for the port**

| topic | result |
|---|---|
| plane streaming | **no patch needed** for E ≤ 81: the game keeps plane columns [camX−96, camX+401) valid and rows are always the full 512 px (§2). Verified on every section at E=40 (and 4 sections at E=20). |
| objects / sprites | ROM patches (§3, `tools/re/woi_wide_patch.py`): sprite-piece clip, despawn window, spawn triggers, map-animation window, one "appear at edge" object. **E must be 1..63.** |
| player push rules | 2P keep-on-screen window and the auto-scroll window widened by E (§1). |
| level edges | runtime mask rule from RAM (§4) — no gameplay change. |
| HUD | SAT entries 0..9 in gameplay: shift −E except PAUSE (SAT 2,3) (§5). |
| pillarbox | widescreen only while `($FFFFD0 & $7F) ∈ {2,3,10,12}` (§6). |
| HINT | only used by the ending credits (per-line VSRAM), unaffected (§7). |

Scripts (all in `tools/re/`; outputs go to `%TEMP%\woi_wide` or `$WOI_WIDE_OUT`):

* `woi_wide_util.py` — helpers: `new_md`, `goto(md, world, entry, mode)` level jump, `drive_cam` (moves
  the camera through the game's own camera code), `mask_rule`, `shot_masked`, `xref`, `grid`.
* `woi_wide_patch.py` — the ROM patches (`patches(E)`, `apply(md, E)`, `apply_bytes(rom, E)`;
  `python woi_wide_patch.py 40` prints the list).
* `woi_wide_check.py` — `PlaneChecker` (stale-tile detector) and `SpriteChecker` (pop-in detector).
* `woi_wide_sweep.py` — plane verification over every section. `woi_wide_objsweep.py` — object pop-in
  sweep. `woi_wide_spawnstats.py` — static analysis of every object placement record.
* `woi_wide_demo.py` — before/after strips (original ROM vs. patched ROM + mask, red-tinted for
  review) for 8 scenarios incl. 2P: forest, backtrack, 2P, edges, boss-room, vertical, sky, water.

States: `tools/states/woi_wide_options.state` (options card board — START starts the game; used by
`goto`), `tools/states/woi_wide_s1_1p.state` (W1 act 0, Mickey 1P).

Tooling notes: `tools/states/woi_stage1.state` actually contains the *options card board*, not stage 1.
`mdt.py` prefers `build/tools/mdtool.dll` when it exists, and that build uses a different save-state
format; `woi_wide_util.py` pins `MDTOOL_DLL=tools/bin/mdtool.dll` so the states load.

**2:1 (E = 64), October 2026.** The port goes up to 2:1. Shifted by up to E near level edges, the view
covers [camX−2E, camX+320+2E) = [camX−128, camX+448): past the streamed columns [camX−96, camX+401)
on both sides. Cells outside them are drawn from the level map by the module's line hook
(`draw_far_columns`, with the column writer's lookup: chunk = layout[(y>>8)*32 + (x>>8)] (byte; the 16-bit
index arithmetic of the 68000 is kept), block = blocks.w[chunk*256 + ((y>>4)&15)*16 + ((x>>4)&15)] & $1FF,
name-table word = tiles.w[block*4 + ((y>>3)&1)*2 + ((x>>3)&1)]; struct +$00 layout, +$04 blocks, +$08
tiles). Planes whose columns a section does not stream ($FC61 bit 5 A, bit 7 B) hold their whole 512-px
picture and are left alone. The sprite clip's left bound is kept at VDP X ≥ 1 (unsigned test; $60−2E would go
negative): pieces starting left of screen x −127 are dropped, which only matters at the very left edge
when the view is shifted fully left (right end of a level). Type 43's reveal at camX+$160 moves out by E
(`$129FE`). E ≤ 63 in the summary above is the 9-bit sprite X limit, lifted by the renderer's 10-bit X.
Checks: `tools/qa/woi21/lookup_check.py` (lookup == streamed name tables, 5.1 M cells, only message boxes
and a Storm Mountain background jump differ), `exact.py` (every plane pixel vs the map, 0 wrong of
≈22 M map-drawn pixels in all sections), the `tools/qa/woi` suite at 64 (`out64/`).

**Plane B during the view shift (October 2026).** Sliding plane B by the same vs as plane A made it drift
against its direction near level edges (its camera moves at the $FCC0 factor: 0 none, 1/2 half, 3 full, 4
double, 5 Y only - `$7F06`). The renderer now takes a per-line plane B shift (`VDP::view_shift_b`): main
lines get floor(camB16.16 + factor x vs) - floor(camB) so they hold exactly still; bands with their own speed
(per-line values written by section code, e.g. the 1.25x bands = ceil(1.25 x camX)) get the same from a small
table in `woi.cpp` (`kBands`, measured with `tools/qa/woi21/band_survey.py`).

---

## 0. Game state / level variables

| RAM | meaning |
|---|---|
| $FFFFD0 | main state, table at $3F2 (bit 7 = "initialised" flag): 0 boot/SEGA $1D6A, 1 new game $1F30, **2 level load $1F5E**, **3 gameplay $200A**, 4 intro $1134, 5 title $19FA, 6 story book $1A36, 9 demo init $21CE, **10 attract demo (2P) $2250**, **12 pause $2146**, 13 options card board $20BF0, 14 game over / password-continue $1FB0C, 15 "Illusion Shot Pictures" $1E18, 16 character select $39DA, 17 ending $222DA, 18 story book between worlds $1A25C |
| $FFFFD2 | VBlank handler index (handler $D74): 0/1 = SAT 10..79 DMA'd from RAM $F300 to VRAM $D450 (gameplay), 2 intro/title, 3 book, 4 menus (full SAT from $F300), 5 ending |
| $FFFFD8 | bit1 = 2-player game; bit0 = 1 → Mickey is player 1 |
| $FFFFDA | world 1..5; $FFFFDE = start entry (table $E2548, loader $AA2A); $FFFFDC = act (from the start record) |
| $FFA000 / $FFA080 | player 1 / player 2 object: X +$10, Y +$14 (16.16), hearts +$38 (byte) |
| $FFFC61 | per-section flags: b0 hscroll table built by level code, b1 Y-focus override ($FD80), b2 X-focus override = auto-scroll ($FD7C), b4/b5 no plane-A rows/columns streaming, b6/b7 no plane-B rows/columns |

Level jump used by the scripts: load the options state, press START, wait for $FFD0 == 2, write
$FFDA / $FFDE / $FFD8, run until $FFD0 == 3. Start entries: W1 0-5, W2 0-18, W3 0-10, W4 0-19,
W5 0-18 (W2 entries 7/9/11 do not start stand-alone).

## 1. Camera

Per-frame routine **$7CEC** (gameplay state $200A → `jsr $7cec`). Two camera structs:
**$FF8500 = plane A (foreground)**, **$FF8560 = plane B**.

| plane A | struct offset | meaning |
|---|---|---|
| $8500/$8504/$8508 | +$00/$04/$08 | pointers: chunk layout (RAM $FA00), block table, 16×16 tile table |
| **$FF8510** | +$10 | **camera X** (16.16, word = px) |
| $8514 | +$14 | X delta of this frame |
| $8518 | +$18 | camera X target |
| $851C | +$1C | target velocity (cleared every frame at $7D36) |
| $8520 | +$20 | previous camera X (streaming compares against it) |
| $8524 | +$24 | X lock (byte ≠0: target does not follow the focus) |
| **$8528 / $852C** | +$28/+$2C | **X clamp min / max** (word = px; compared as longs) |
| $8530 / $8534 | +$30/+$34 | dead zone: focus kept in [target+$8530, target+$8534] (148..164; W2 act 0: 84..100) |
| **$FF8538** | +$38 | **camera Y**; +$3C delta, +$40 target, +$44 vel, +$48 prev, +$4C lock |
| **$8550 / $8554** | +$50/+$54 | **Y clamp min / max** |
| $8558 / $855C | +$58/+$5C | Y dead zone |

Plane B camera: X **$FF8570**, Y **$FF8598** (same layout). Its target moves by the FG delta times a
factor chosen by $FCC0 ($7F06: 0 none, 1 ½x/½y, 2 ½x/1y, 3 1:1, 4 2x, 5 Y only).
$FFFCCC / $FFFCCE = screen-shake offsets (subtracted from the plane-A scroll, added to sprites).

Flow of $7CEC: $7D6E focus → $7E4A (Y then X): `target += focus−target−deadzone` when outside the
dead zone, then **clamp** ($7E92–$7EBE: `blt min → min`, `bge max → max`) → $7F86 current moves
toward target, **max 6 px/frame** ($7F9C) → $7FDE streaming (plane A) → $833E level script (pointer
$FD78, state at $8908) → plane B ($7F06/$7EC0/$7F86/$7FF8) → $8182 hscroll table → $7D46 2P window.

Level header table **$E254A** (world $FFDA, act $FFDC; 50-byte records, loaded by $9D9C): byte
parallax mode ($FCC0), byte flag, dead zones ($8534, $8530, $855C, $8558), then per plane: layout
index, **max X, min X, max Y, min Y**, tile ptr, block ptr; then long → $FD94. Examples: W1 act 0
X 0..2752 Y 0..784; boss rooms X 0..0; W2 acts 1-11 rooms X 0..192 (one 512-px plane);
W3 act 5 X 0..6848.

**2-player camera.** $7D6E: in 2P the focus X = (P1 X + P2 X)/2 ($7D8A); focus Y ($7DCE) = average,
or the Y of the player selected by $FCE2 when one stands on ground (bit1 of +$3D). Bits 5/6 of $FFD8
can pin the focus to one player.
**Keep-both-on-screen / push rule** ($7D46, every frame):
```
007d46: move.w $8510.w,d0 / addi.w #$10,d0  / move.w d0,$fce8.w   ; left  = camX+16
007d52: addi.w #$120,d0  / move.w d0,$fcea.w                      ; right = camX+304
007d5a: move.w $8538.w,d0 / addq.w #8,d0 / move.w d0,$fcec.w      ; top   = camY+8
007d64: addi.w #$d0,d0  / move.w d0,$fcee.w                       ; bottom= camY+216
```
$BF8A (per player): clamp X to [$FCF2, $FCF0] (level/scripted limits) and, **in 2P only**
($FFD8 bit1), to [$FCE8, $FCEA]; $BFC2 flags a player outside [$FCEC, $FCEE] (bit1 of +$3D).
→ patch `p2_clamp_*` widens the X window to [camX+16−E, camX+304+E] (symmetric, so the averaged camera
stays centred). Verified: with P1 holding RIGHT and P2 holding LEFT the players stop at relX 344 / −24
at E=40 (original 304 / 16), i.e. 16 px from the wide edges (`woi_wide_demo_2P.png`).

**Auto-scroll push rule** (level script $906C, W5 act 6): while the scroll runs it writes every frame
`$FCF2 = camX+16` (player min X) and `$FCF0 = camX+16+$128` (max) at $90B4/$90BC and $90E2/$90EA
→ patch `autoscroll_*` → [camX+16−E, camX+312+E] (verified: player pushed at relX −24 instead of 16).

## 2. Tile streaming (no patch needed)

Planes: A at VRAM $C000, B at $E000, 64×32 cells (512×256), hscroll table VRAM $D000 (reg 11 = 3,
per line), SAT $D400. Both planes use the same routines (a1 = camera struct):

* **Column streaming $8012** (called by $7FDE for A / $7FF8 for B): when `(X ^ prevX) & $FFF0 ≠ 0`,
  draw one 16-px block column at `X` (moving left) or `prevX+$200` (moving right) via $810A, which
  first calls **$807A: `X −= $60` (96), `Y −= $10`** (both clamped at 0). The column is 16 blocks =
  **256 px = the whole plane height**.
  ```
  008022: cmp.w $10(a1),d1 / blt.b $8034          ; prev < cur → moving right
  008028: move.w $10(a1),d0 / move.w $38(a1),d1 / bra.w $810a      ; left: column at X
  008034: move.w $20(a1),d0 / addi.w #$200,d0 / ... / bra.w $810a  ; right: column at prevX+512
  00807a: subi.w #$60,d0 / bhi.b + / moveq #0,d0 / subi.w #$10,d1 / bhi.b + / moveq #0,d1
  ```
* **Row streaming $8046**: when Y crosses 16 px, draw a row at `Y` / `prevY+$100` (−16 by $807A) of
  **32 blocks = 512 px = the whole plane width**, starting at camX−96 ($808C).
* **Initial fill** $AB1C/$AB42: 16 rows via $808C → whole plane.
* Camera speed ≤ 6 px/frame, so one column/row per frame always suffices.

Hence the planes always hold blocks floor(X/16)−6 … floor(X/16)+25, i.e. **[X−96, X+401) at worst**
horizontally and the full 256-px height vertically. The (320+2E) view [X−E, X+320+E) is inside it
for **E ≤ 81** (left margin 96−E, right margin ≥ 81−E), without exceeding the 512-px wrap. E=20 and
E=40 are fine unpatched; vertical scrolling is unaffected by E. (QuackShot needed its window moved
to X−96; WOI already does that.)

Exception at level edges: $807A clamps at 0, so at camX < 96 the columns left of world 0 keep plane
content from the other end of the 512-px window → masked by §4.

**Verification (automatic).** `woi_wide_check.PlaneChecker` samples every frame, for planes A and B,
the nametable entry shown at every 8-px position of the 400-px view, in world coordinates
(screen x − hscroll(line), honouring the reg 11 H-scroll mode), and flags an observation in the
extension columns that differs from an observation of the same world cell inside the original 320
view within ±90 frames. Plane A outside the camera bounds is excluded (that is §4).
Sensitivity check: with a 112-px extension it reports 39 686 stale cells, exactly at screen
x < −96 and ≥ 408, matching the analysis.
`woi_wide_sweep.py 40` drives the camera through **every section** (73 start entries): horizontal
passes (right, left, right) at min/mid/max Y and vertical passes (down/up) at min/mid/max X, through
the game's own camera/streaming code (lock flags + target; player kept at the view centre, hearts
refilled, A tapped to dismiss message boxes). Result: **0 stale plane-A/B cells in the extension area
in all sections** (≈75 M extension observations). The only mismatches were in W5 act 1 (card maze)
during a warp-door transition caused by the sweep teleporting the player into a door; the whole
screen (inside 320 too) is garbage for those frames, so it is not widescreen related.
E=20 spot check (W1 act 0, W2 act 15, W3 act 5, W4 act 10): 0 stale cells. Visual check: sweep strips
(24 frames per section) and contact sheets of all 73 section starts.

Map-animation areas ($ABB2; $B000 entries of $20 bytes, count $FCFE): triggered block animations
(breaking/collapsing blocks; W1 acts 2/4, W2 acts 15/16, W3 act 7, W4 acts 9/12, W5 act 6) only
advance while the area intersects blocks [camX>>4, (camX>>4)+20] × [camY>>4, +14] ($ADE2); outside
they freeze (RAM block data and nametable). Patch `anim_*` widens the X range to the (320+2E) view
(unit-tested with breakpoints at $ADEC/$ADFA in W2 act 15: d2 = (camX−E)>>4 clamped ≥ 0, right
bound +25 for E=40). These animations are event-triggered, so no dynamic before/after capture.

## 3. Objects / sprites

* Slots: $9C00 (16×$40, level-permanent, spawned at load by $20B86), $9000 (48×$40, spawned from the
  placement grid), $A100 (22×$40, children/effects), players $A000/$A080. Update $B3F6; handlers via
  table ($FD74) = $B4C0 in gameplay (type n → long at $B4C0+4(n−1)).
* **Placement grid / spawner $209F0** (a4 = $8500): the level's objects are copied to RAM $8000
  ($20B48, source $AA39A): header {width, height in 128-px cells}, one byte per cell = group, groups
  of 9-byte records {flags, type, param, spawned-flag, ?, Y.w, X.w}.
  * Horizontal trigger **$209FA**: when camX crosses a 128-px boundary, spawn the cell column that
    contains `camX+$180` (moving right, $20A8C) or `camX−$71` (moving left, $20A92), 5 cell rows
    floor(camY/128)−1..+3. Right: objects appear 64..191 px beyond the 320 edge; left: 0..127 px.
  * Vertical trigger **$20A12**: same with `camY+$180` / `camY−$71`, 5 cell columns
    floor(camX/128)−1..+3 ($20A4A).
  * Level start **$20BBC**: 5 columns at camX−$71+{0,$80,…,$200}.
  * Record flag bit0 of byte 2 prevents double spawns; object +$2E points at the record.
* **Despawn** = whole-object cull in the sprite builder **$BA34**: `d4 = objX+$80−camX`,
  `d3 = objY+$80−camY`; if d4 or d3 ∉ [0,$280) → `bcc.w $F600` → delete the object and re-arm its
  record ($F600/$F60E). Objects live while relX ∈ [−128, 512) (bit4 of +$0C: never culled).
* **Sprite builder $B9CA**: display lists $F200/$F280/$F282/$F284; screen position =
  obj+$80−cam (unless bit3 of +$0C = screen coordinates) + $FCCC/$FCCE; pieces go to RAM $F300,
  links start at 11 ($FC65 = $0A), max $45 pieces → **SAT entries 10..78**.
  Per-piece clip (unless bit5 of +$0C): **$BBC6 Y: VDP y ∈ [$60,$160)**, **$BBE4 X: VDP x ∈
  [$60,$1C0)** = screen x ∈ [−32, 320) → every piece right of 320 was dropped.
  ```
  00bbe4: btst #5,$c(a0) / bne.b $bbf8
  00bbec: cmpi.w #$60,d0  / bcs.b $bbfc      ; x < -32  -> drop piece
  00bbf2: cmpi.w #$1c0,d0 / bcc.b $bbfc      ; x >= 320 -> drop piece
  ```
* Per-object "wake-up" windows (not patched, see consequences): $F7C2 (type 167), $FFF2 (149),
  $12036 (45) activate when objX ∈ [camX+16, camX+304]; $11F08, $126A2, $130CE, $1335A, $13C46,
  $13D84, $14124, $18D6E/$18DCC/$18E50, $1A148 use [camX, camX+320]; $10FA4 [−16, 336];
  $11310 [−64, 384]; $180FE [0, 384]; $13854 (type 18) [32, 288]; $129FA (type 43) unfreezes at
  camX+352; **$1B4B4 (type 65) stays hidden+frozen until objX ≤ camX+320** (pop-in at the old edge →
  patched).

### Patch list (`woi_wide_patch.py`; bytes for E=40 / E=20)

| name | address | original | new (E=40) | new (E=20) | purpose |
|---|---|---|---|---|---|
| checksum | $0004FC | `6602` bne.b $500 | `4E71` | same | skip the boot checksum red screen |
| spr_clip_left | $00BBEC | `0C40 0060` | `0C40 0038` | `0C40 004C` | piece clip: VDP x ≥ $60−E |
| spr_clip_right | $00BBF2 | `0C40 01C0` | `0C40 01E8` | `0C40 01D4` | piece clip: VDP x < $1C0+E |
| obj_cull | $00BAB6 | `0C44 0280 6400 3B44 4A44 6B00 3B3E 0C43 0280 6400 3B36 4A43 6B00 3B30` | `0644 0028 0C44 02D0 6400 3B40 0444 0028 0C43 0280 6400 3B34 4E71 4E71` | `0644 0014 0C44 02A8 6400 3B40 0444 0014 0C43 0280 6400 3B34 4E71 4E71` | despawn X window [−128−E, 512+E) |
| spawn_h_hook | $0209FA | `302C 0010 362C 0020 3400` | `4EF9 000F FC00 4E71 4E71` | same | jmp to the new trigger |
| spawn_h_code | $0FFC00 | `FF`×48 | see below (E=$28) | E=$14 | trigger on camX±E |
| spawn_v_cols | $020A4A | `302C 0010 0240 FF80 3400` | `342C 0010 0442 0028 4E71` | `342C 0010 0442 0014 4E71` | vertical trigger columns from camX−E |
| spawn_v_count | $020A60 | `7E04` | `7E05` | same | 6 columns instead of 5 |
| spawn_init | $020BD6 | `0C47 0280` | `0C47 0300` | same | level-start spawn: 6 columns |
| anim_hook | $00ADE6 | `3438 8510 E842` | `4EB9 000F FC40` | same | jsr helper |
| anim_code | $0FFC40 | `FF`×16 | `3438 8510 0442 0028 E842 6A02 7400 4E75` | `… 0442 0014 …` | d2 = max(0, (camX−E)>>4) |
| anim_right | $00ADF6 | `0642 0014` | `0642 0019` | `0642 0017` | right bound 20+ceil(2E/16) blocks |
| p2_clamp_left | $007D4A | `0640 0010` | `0640 FFE8` | `0640 FFFC` | 2P push: camX+16−E |
| p2_clamp_right | $007D52 | `0640 0120` | `0640 0170` | `0640 0148` | 2P push: +$120+2E |
| autoscroll_min1/2 | $0090B4, $0090E2 | `0640 0010` | `0640 FFE8` | `0640 FFFC` | auto-scroll player min |
| autoscroll_max1/2 | $0090BC, $0090EA | `0640 0128` | `0640 0178` | `0640 0150` | auto-scroll player max |
| obj65_appear | $01B4B8 | `0640 0140` | `0640 0168` | `0640 0154` | type 65 appears at camX+320+E |

New code at $0FFC00 (ROM $0FFAFA–$0FFFFF is $FF padding; no references into it):
```
0ffc00: 302c0010      move.w $10(a4),d0      ; camX
0ffc04: 362c0020      move.w $20(a4),d3      ; prevX
0ffc08: 3400          move.w d0,d2
0ffc0a: 9443          sub.w  d3,d2           ; delta (d2 = direction test at $20A88)
0ffc0c: 6b0a          bmi.b  .left
0ffc0e: 0640 00EE     addi.w #E,d0
0ffc12: 0643 00EE     addi.w #E,d3
0ffc16: 6008          bra.b  .chk
0ffc18: 0440 00EE     .left: subi.w #E,d0
0ffc1c: 0443 00EE     subi.w #E,d3
0ffc20: b143          .chk: eor.w d0,d3
0ffc22: 08030007      btst #7,d3             ; crossed a 128-px boundary?
0ffc26: 6706          beq.b .ret
0ffc28: 4ef900020a7e  jmp $20a7e             ; original spawn-column code (+$180 / -$71)
0ffc2e: 4e75          .ret: rts
```
Rationale: the despawn window [−128, 512) is what limits how far ahead objects may be spawned, so it
is widened by E on both sides (`obj_cull`; the `tst/bmi` pairs were redundant with the unsigned `bcc`,
which frees the bytes); the horizontal trigger is then evaluated on camX+E (right) / camX−E (left),
reproducing the original spawn geometry relative to the widened edges (right: 64..191 px beyond the
edge, left: 0..127). The vertical trigger and the level-start spawn cover the extra column the wider
view can touch. The per-piece clip keeps VDP x in [56, 488) at E=40 — inside the 9-bit 0..511 range
and never 0 (x = 0 would trigger sprite masking). **E ≤ 63** is required ($1C0+E ≤ $1FF).
Unit test (breakpoint $20A88, W1 act 0): original fires at camX 132/258/384/516/642 (right) and
634/508/382/250/124 (left); patched at 90/216/348/474/600 (camX+40 crosses 128k) and
676/550/418/292/166 (camX−40 crosses).

### Verification

* **Static** (`woi_wide_spawnstats.py`, all 1042 trigger-spawned placement records, sprite
  half-width 16): records that appear inside the visible view at spawn time —
  original game at 320: 49 (moving right) / 153 (moving left); **original ROM through the E=40 view:
  85 / 371**; **patched, E=40: 58 / 153**; E=20: original 64 / 232, patched 54 / 153.
  Leftward motion is back to exactly the original game's behaviour; rightward has +9 records: "ambush"
  objects whose record lies in a cell column far ahead of their position (e.g. type 45 in W2 acts 0/16:
  placed 272..448 px left of their cell so they spawn behind the camera). Spawning E px earlier brings
  them E px closer to the left edge. Not fixable generically (185 of 1519 records are placed outside
  their own cell).
* **Dynamic** (`woi_wide_objsweep.py 40`, every section, camera right/left/right at min and max Y;
  objects that spawn, despawn or un-hide ≥ 8 px inside the view; items collected by the teleported
  player excluded): **base (original game, 320 view) 170, original ROM at E=40 270, patched at E=40
  175.** The remaining differences: type 45 ambushers (W2 acts 15/16, above), type 65 appearing at the
  new edge exactly like it did at the old one (W4 act 8), and type 31 in W1 act 4 (spawn+despawn in
  the same frame = an object replacing itself, not visible). E=20 spot checks show the same pattern.
* **Screenshots** (`woi_wide_demo.py`): `forest` (enemies drawn in the right extension — the original
  drops every piece at x ≥ 320), `backtrack` (objects already present in the left extension when
  walking back), `2P` (players spread to the wide edges).
* **Attract demo** (2P, recorded input) with the patches: stays in sync; Mickey takes one extra hit
  around frame 3590 (an enemy is alive slightly earlier); final positions differ by 1 px.

**Gameplay consequences (kept minimal):** objects are spawned E px earlier in the direction of motion
and survive E px longer when left behind (slightly more slot pressure; 48 slots); the per-object
wake-up windows above are unchanged, so most enemies in the extension columns are visible but idle
until they enter the original 320 area (just as they sit off-screen in the original); 2P / auto-scroll
players can use the whole width. Nothing else changes.

## 4. Level edges / locked camera — runtime mask rule (recommended)

The camera clamp is the target clamp at $7E92: `target ∈ [$8528, $852C]` (plane A; Y: $8550/$8554),
loaded from the level header ($9E64/$9E68) and changed by level scripts in only two places:
`$8A78 move.w d0,$8528` (W3 act 5: one-way lock at X=$15B7) and `$8F36 move.w #$1000,$8528` (W4 act
10: lock at $1000 before the vertical auto-scroll). Outside [minX, maxX+320) the plane shows wrapped
or unused layout (e.g. W1 act 0 start: tree bark from the far end of the 512-px window; beyond the
right end, unrelated layout), so the renderer must black it out:

```
if widescreen active (§6):
    x   = s16(RAM16[$FF8510]) - s16(RAM16[$FFFCCC])   # displayed plane-A X (camera minus shake)
    lo  = RAM16[$FF8528] ; hi = RAM16[$FF852C]         # camera X clamp (or header values, below)
    mask_left  = clamp(E - (x - lo), 0, E)             # black columns at the left of the view
    mask_right = clamp(E - (hi - x), 0, E)             # black columns at the right
```
Apply the mask to planes and game sprites but **not** to the HUD (§5), or the HUD vanishes at level
starts. Using the *header* bounds instead of the live clamp (snapshot $8528/$852C while $FFD0 == 2,
before the level scripts run) avoids a black bar popping in at the two script locks above (the area
behind those locks is ordinary, correctly streamed level content). Both variants were checked on the
contact sheets of all 73 section starts, `woi_wide_demo_edges.png` and `woi_wide_demo_boss-room.png`.

Per situation:
* **Stage start / end**: camX = 0 / maxX → 40 px masked on that side (e.g. W1 act 0; W3 act 2 starts
  at the right end).
* **Boss arenas / single-screen rooms** (header X 0..0: W1 act 5, W2 acts 2/4/6/8/10 and bosses
  17/18, W3 act 8, W4 act 13, W5 act 15): both sides fully masked → exactly the original 320 view.
  X 0..34 (W3 act 1): 40/6 at camX 0, 6/40 at camX 34.
* **512-px rooms** (X 0..192: W2 acts 1/3/5/7/9/11, W3 acts 6/9/10, W4 act 14, W5 acts 5/7/12/16):
  masked only within E of either end.
* **Vertical sections**: W2 act 14 (X 0..0, Y 0..1312) is a shaft → pillarboxed by the rule;
  W2 act 15 (1728×1312), W4 acts 0/1 (1216×1056) scroll both ways normally. No vertical mask needed.
* **Auto-scroll**: W5 act 6 (script $906C, $FC61 bit2, ≈1.19 px/frame) and W4 act 10 (vertical
  auto-scroll $9752 after camX ≥ $1000): rule unchanged; push windows patched (§1).
* **Flying carpet** (W2 act 0, W4 act 8 = start entry 10, 4800 wide), **underwater** (W3 acts 0/2,
  W5 act 10), **ship** (W3 act 7): ordinary follow camera, rule unchanged.

Alternative considered and **not recommended**: clamping the camera E px further inside (min+E, max−E
when max−min ≥ 2E, trampoline at $7E92). Level scripts and objects compare camX with fixed thresholds
(`cmpi.w #$960/#$bb0/#$e10/#$1000/#$576,$8510` at $89E0, $8A14, $8EC0, $8F28, $8F7A; objects at
$3CFE…) and some sections start at maxX (W3 act 2), so shifting the reachable camera range risks
breaking events; the mask costs no gameplay change.

## 5. HUD

In gameplay the VBlank handler ($DF6 for $FFD2 = 1; $DB8 for 0) DMAs RAM $F300 to VRAM $D450, i.e.
**SAT entries 10..79 are game sprites and entries 0..9 are never touched by the sprite builder.**
Entries 0..9 are written once per level by **$1E3DA** (called from the level load at $1FA2; VDP write
at $1E40E) from ROM tables $1E490 (entries 0-3), $1E4B0 (Mickey 1P) / $1E4C8 (Donald 1P) / $1E4E0
(2P, Mickey = P1) / $1E510 (2P, Donald = P1). Hearts/digits are updated by rewriting their
*patterns*, not the SAT (verified: SAT $D400–$D44F byte-identical over 1500 frames of W1 while the
hearts went 5→1, and over the whole 2P attract demo). Only other writers: $2154/$2160 and $21A6/$21B2
(pause: Y of entries 2 and 3 = $E8 shown / $1E8 hidden).

| SAT | content | x (screen) | y | tiles (pal 0, priority 1) | anchor |
|---|---|---|---|---|---|
| 0 | "TRIES" label | 120 | 12 | $0A6 (3×1) | left |
| 1 | tries digits | 156 | 8 | $0AA (2×2) | left |
| 2, 3 | "PAUSE" (y 360 = hidden, y 104 while paused) | 136, 160 | 104 | $004, $00A (3×2) | **centre** |
| 4, 5, 6 | P1 icon ("M" $09E / "D" $0A2), hearts | 16, 36, 68 | 8 | $09E/$0A2, $6B4, $6BC | left |
| 7, 8, 9 | 2P only: P2 icon + hearts (second row) | 16, 36, 68 | 28 | $0A2/$09E, $6C4, $6CC | left |

In 2P both HUD rows are stacked at the top-left and "TRIES" is shared — there is no right-hand group.
No boss energy bar exists; item pop-ups are ordinary objects; the W2 "magic" message box is drawn in
plane A, centred in the 320 view.

**Recognition rule for `sprite_xoff(index, attr, x, y)`** (only while widescreen is active, §6):
```
if RAM16[$FFFFD2] in (0, 1) and index <= 9:          # gameplay VBlank: SAT 0..9 = HUD
    return 0 if index in (2, 3) else -E_hud           # PAUSE centred, everything else left-anchored
return 0
```
`E_hud = E` normally (E=20 → −20, E=40 → −40); for sections narrower than the wide view use
`E_hud = min(E, (maxX - minX) / 2)` with the header bounds (§4) so the HUD stays over the picture in
single-screen rooms (X 0..0 → 0). Optional cross-check: tile index in $004–$00F, $09E–$0AF or
$6B4–$6CF with the priority bit set.

## 6. Non-gameplay screens

Checked at E=40: SEGA logo (state 0), "Illusion Shot Pictures" (15), intro room pan "Cast" (4),
title (5), story book (6 and 18), options card board (13), character select (16), game over /
continue-password (14) and the ending (17: theatre stage with audience, then credits over a forest)
are all authored for 320: the extension shows the backdrop colour, wrapped plane content (book page,
theatre walls) or nothing useful → **pillarbox them**. Condition for widescreen:

```
widescreen = ((RAM16[$FFFFD0] & $7F) in (2, 3, 10, 12))   # level load/fade-in, gameplay, attract demo, pause
```
State flow: new game 1 → 2 → 3; from gameplay → 18 (world clear, story book), 14 (game over),
16 (character select), 2 (death / next section), 12 (pause). State 2 already shows the level fading
in, so switching there avoids an aspect jump. $FFD2 alone is not sufficient (it is 1 also on the
"Illusion Shot Pictures" screen).

## 7. Special cases

* **Per-line hscroll**: table RAM $F680 (224 × {A,B}), built by $8182 (−camX / −bgX for all lines, then
  up to 4 bands from $8D00: {word flag (<0 plane A, >0 plane B), world yTop, yBot, pointer to per-line
  values}) and DMA'd by $8308; sections with $FC61 bit0 build it with their own code (e.g. $9404:
  plane-B per-line parallax from $FF5A00; W1 act 0: plane-B band at Y $3B0–$400 from $8B00). In every
  section observed with bit0 set (W1 act 0, W2 act 18, W3 acts 0/2/4/6/9/10, W4 acts 2/10,
  W5 acts 6/10/11)
  bit7 is set too, i.e. plane-B column streaming is off and the 512-px plane B is drawn once, so lines
  scrolling at their own speed are valid at any width — confirmed by the plane-B checks of the sweep
  (0 stale cells) and visually (`woi_wide_demo_sky.png`, `_water.png`, sweep strips).
* **HINT ($FFAF80)**: installed only by $23B9E (ending/credits, state 17, NTSC only —
  `tst.b $FFCE`), handler copied from $23BC8: every line writes VSRAM word 0 (plane-A vscroll) from
  the table at $FF6000 (self-modifying source address, +2 per line) and disables itself at V=$DE
  (`move.w #$8AFF`). It is the credits text "wobble" — a full-line effect with no horizontal
  dependency, on a pillarboxed screen. (In gameplay reg 0 = $04, HINT off; RAM $FFAF80 is then used as
  a 30-byte object table by $55EA/$561C.)
* **Message boxes** (W2 act 0): drawn into plane A at the camera, centred in the 320 view → centred.
* **Sprite limit**: the game builds at most 69 pieces (SAT 10..78); render with `sprite_limit=False`
  (per-line limits would otherwise drop pieces in the extension).
* **2P sprite priority swap** ($BF06/$FC6C) and screen shake ($FCCC/$FCCE) are width-independent.
* Nothing found that breaks at E=40 with the patches applied. Known cosmetic leftovers: enemies idle in
  the extension until they reach their 320-based wake-up window; a few "ambush" objects appear inside
  the left extension (§3); type 43 unfreezes at camX+352 (8 px inside the E=40 view).
