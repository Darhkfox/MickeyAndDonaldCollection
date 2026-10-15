# Analysis tooling

Everything needed to reverse-engineer the two ROMs headlessly.

## ROMs
- `Mickey Mouse - Castle of Illusion (U) [!].bin` — Castle of Illusion, USA, 512 KB (`mdt.COI`, `m68dis` key `coi`)
- `Quack Shot Starring Donald Duck (JUE) (REV 00) [!].bin` — QuackShot, World REV00, 512 KB (`mdt.QS`, key `qs`)
- `World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin` — World of Illusion, USA, 1 MB (`mdt.WOI`, key `woi`)

## `tools/mdt.py` — scriptable emulator (wraps `tools/bin/mdtool.dll`)
Our own Mega Drive core (68000/Z80/VDP exact enough to run both games; sound chips are stubbed in this
DLL build, which does not matter for analysis). NTSC, 60 fps, 3-button pad on port 1.

```python
import sys; sys.path.insert(0, r'K:\projects\castle of illusion\tools')
from mdt import MD, COI, QS
md = MD(COI)
md.run(600)                 # run frames
md.press('START', 3, 60)    # hold START 3 frames, then release and run 60 frames
md.hold('RIGHT+C')          # hold buttons until changed: UP DOWN LEFT RIGHT A B C START joined by '+'
md.hold2('LEFT')            # player 2 pad (port 2), same syntax
md.run(120); md.hold(0)
md.png(r'...\shot.png', 2)  # screenshot (scale 2). View PNGs with the Read tool.
md.ram16(0xFFF400); md.set16(0xFFF400, 3)   # RAM access (68000 addresses)
md.ram / md.vram / md.cram / md.vsram / md.regs / md.zram / md.rom   # raw arrays (live, writable)
md.cpu()                    # 68000 registers dict (d0..a7, pc, sr)
md.z80()                    # Z80 registers + bank/reset/busreq
s = md.save(); md.load(s)   # save states (bytes) - only valid with this DLL build
md.set_ext(40, sprite_limit=False)  # render 40 extra px each side (widescreen preview: 400x224)
md.patch16(addr, value)     # patch the loaded ROM copy (word)
md.coverage(True); ...; md.covered(lo, hi)   # executed instruction addresses with counts
md.bp(addr); md.run(n); md.bp_hits()         # log registers every time PC hits addr
md.watch(lo, hi); md.run(n); md.watch_hits() # log 68000 writes to RAM range: frame, pc, addr, size, value
md.unwatch(); md.bp_clear()
```
Frame buffer colour-code note: `md.pixels()` returns (w, h, list of 0xAARRGGBB).

## `tools/m68dis.py` — disassembler (capstone)
```
python tools/m68dis.py coi 0x200 40        # 40 instructions at $200 with bytes
python tools/m68dis.py qs xref 0xFFEE00     # instructions referencing an address (abs.w/abs.l)
python tools/m68dis.py coi calls 0xCF92     # JSR/JMP/BSR/Bcc targeting an address
```
From Python: `from m68dis import dis, insns, xrefs, calls, u16, u32, rom`.
Capstone prints absolute-short RAM addresses as e.g. `$f400.w` meaning `$FFF400`.

## Save states
`tools/states/*.state` hold handy starting points (made with the current DLL):
- `coi_forest.state` — Castle of Illusion, Enchanted Forest act 1, shortly after start
- `qs_duckburg.state` — QuackShot, Duckburg, shortly after start
- `woi_stage1.state` — World of Illusion, 1-player Mickey, first stage shortly after start

## Known facts
- Both games run in H40 (320x224), NTSC.
- Both use the VDP window plane for the bottom status bar (reg 18 = $98 → window from line 192).
- COI gameplay planes are 64x64 cells; QS gameplay planes are 64x32 cells.
- World of Illusion (1 MB ROM, supports 2 players simultaneously): gameplay planes 64x32 cells, per-line
  hscroll, no window plane; the HUD (character icon, hearts, TRIES) is drawn with sprites at the top.
  Its VBlank handler is at $D74 (dispatch on $FFFFD2); HINT vector points to RAM ($FFAF80).
