# Mickey & Donald Collection — PC Edition

A Windows port of three Sega Mega Drive / Genesis classics, with a launcher, widescreen up to 21:9,
modern display options, gamepad support, cheats, level select, save states, fast forward / rewind and a
music player:

- **Castle of Illusion Starring Mickey Mouse** (1990) — 1 player
- **QuackShot Starring Donald Duck** (1991) — 1 player
- **World of Illusion Starring Mickey Mouse and Donald Duck** (1992) — 1-2 players simultaneous

The games themselves run from **your own copies of the original ROMs**. The port contains no game code or
assets; it runs the ROMs on a built-in, from-scratch Mega Drive core, so they play exactly like the
originals, then adds game-specific patches and a PC frontend.

## Getting started

1. Download the zip from the Releases page and unzip the `Mickey and Donald Collection` folder anywhere
   (nothing needs installing: one self-contained `DisneyClassics.exe`).
2. Put the ROMs next to `DisneyClassics.exe`, or in a `roms` folder beside it:

   | Game | Expected dump | Size | CRC32 |
   |---|---|---|---|
   | Castle of Illusion | `Mickey Mouse - Castle of Illusion (U) [!].bin` | 512 KB | `BA4E9FD0` |
   | QuackShot | `Quack Shot Starring Donald Duck (JUE) (REV 00) [!].bin` | 512 KB | `88C8DD94` |
   | World of Illusion | `World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin` | 1 MB | `921EBD1C` |

3. Run `DisneyClassics.exe`. The first launch opens fullscreen; F11 or Alt+Enter switches to a window.

ROMs are found by their contents (CRC32), so file names don't matter. Raw `.bin` / `.md` / `.gen` dumps,
interleaved `.smd` dumps, dumps with a 512-byte copier header and all of these inside a `.zip` work. ROMs
copied in while the launcher is open are picked up within a second.

**These exact versions are needed** (the USA cartridges; QuackShot's is the USA / World first revision): the
widescreen patches, level select and cheats are matched to their internals. A game whose ROM is missing
shows *ROM not found* on its card, with the file it needs. Any other dump of the game is recognised from
the ROM's internal header, and the card says what it is instead: the European or Japanese version, another
revision (REV 01), or a modified copy / bad dump. The other games still work.

To start a game directly, give `DisneyClassics.exe` the argument `--game coi`, `--game qs` or `--game woi`
(e.g. in a shortcut).

## Features

**Launcher**
- One card per game with a live attract-mode preview, plus Options, the Music Player and Credits.

**Widescreen**
- 16:10 (360×224), 16:9 (400×224), 2:1 (448×224) and 21:9 ultrawide (536×224, fills 3440×1440 and
  2560×1080 screens) for Castle of Illusion and QuackShot; World of Illusion goes up to 2:1. The in-game
  menu only offers the widths the running game supports.
- More of each level is really drawn at the sides: every game is patched so enemies and items spawn, move
  and are drawn across the whole width, instead of popping in at the edges of the old 320-pixel screen.
  Where the view reaches past the columns the console's 512-pixel background layers hold (21:9, and 2:1 in
  World of Illusion), the extra columns are drawn straight from the level map.
- The camera stays inside the level where it can; level edges and single-screen boss rooms are masked.
- The status bar / HUD is re-anchored to the screen edges (can be switched off).
- Title screens, menus, maps and cut-scenes stay 4:3 automatically.
- The width (and cheats) can be changed at any time, also mid-level: the patches are swapped at the next
  frame where the CPU isn't inside code that changes, and save states remember the width they were made at.

**Video**
- Borderless fullscreen or windowed, monitor selection (by name; dragging the window to another screen
  selects it), window size 1×–6×, integer scaling, square / CRT (4:3) pixels or stretch.
- Filters: nearest, sharp bilinear, bilinear, scanlines, CRT. Direct3D 11.
- Sprite flicker removal (no sprites-per-line limit), V-Sync, FPS counter.

**Colours**
- Profiles: Accurate (measured Mega Drive output levels), Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia,
  Handheld LCD.
- Brightness, saturation and gamma.

**Audio**
- YM2612 + SN76489 emulation, volume, the Model 1 / Model 2 output filter, the YM2612 "ladder effect".

**Music player & sound test**
- Every music track, sound effect and voice of each game, played by the game's own sound driver, with
  channel meters.

**Level select**
- Castle of Illusion: 28 entries (every act, the Library's tea cups, each boss and the final battle).
- QuackShot: 48 areas, with the story items and flags Donald would have at that point.
- World of Illusion: 38 stages, as Mickey, Donald or 2 players (stages not on that route are greyed out).

**Cheats** (pause menu, applied at once)
- Castle of Illusion: invincibility, infinite power, tries, items and continues, start with full power, and
  the developers' hidden level select on the OPTIONS screen.
- QuackShot: invincibility, infinite health, lives and ammo, all weapons / plungers, endless or permanent
  Quack Attack, and the developers' hidden free-movement (fly) mode.
- World of Illusion: invincibility, infinite hearts and tries, maximum hearts (both players).

**Other**
- Two players in World of Illusion: a second gamepad, or a second set of keyboard keys.
- Keyboard and gamepads: Xbox-compatible pads (XInput) and other USB pads (DirectInput), fully rebindable
  for both players, hot-plugging.
- 5 save-state slots per game (F5 / F9), fast forward (hold Tab or the right trigger, 4×), rewind (hold R
  or the left trigger, up to the last minute of play), screenshots (F12). Fast forward and rewind can each
  be switched off.
- Version 1.0 (shown in the launcher); the Credits page has links to the author.

## Default controls

| Mega Drive | Keyboard (P1) | Keyboard (P2, World of Illusion) | Xbox pad |
|---|---|---|---|
| D-Pad | Arrow keys | I J K L | D-Pad / left stick |
| A | Z | U | X |
| B | X | O | A |
| C | C | P | B |
| Start | Enter | Backspace | Start |
| Pause menu | Esc | | Back / View |
| Fast forward (hold) | Tab | | Right trigger |
| Rewind (hold) | R | | Left trigger |

| Shortcut | Keyboard |
|---|---|
| Quick save / load | F5 / F9 |
| Fullscreen | F11, Alt+Enter |
| Screenshot | F12 |

Player 1 uses the first connected gamepad plus the keyboard; player 2 the second gamepad or the P2 keys.
Each player's keys, buttons and pad can be changed under *Options → Controls*. A key or button bound to a
game button keeps that role (e.g. a trigger mapped to A no longer rewinds). Tools such as DS4Windows or
Steam Input can make one controller appear twice; if one pad moves both players, pick each player's
*Device* by hand.

## Files

Settings are stored in `settings.ini`, save states in `saves\`, screenshots in `screenshots\`. These live
next to the exe (the folder is portable), or in `%LOCALAPPDATA%\Mickey and Donald Collection` if that
folder isn't writable. Delete `settings.ini` to restore the defaults.

## Building from source

Requirements: Windows 10/11, Visual Studio 2022 (or the Build Tools) with the C++ workload, CMake 3.20+.
There are no third-party dependencies.

```
build.bat                                         :: builds build\bin\DisneyClassics.exe (Release, static CRT)
package.bat                                       :: dist\Mickey and Donald Collection: the exe, per-game
                                                  ::   shortcuts and the ROMs in this folder, for local play
powershell -ExecutionPolicy Bypass -File release.ps1
                                                  :: dist\Mickey-and-Donald-Collection-v1.0-win64.zip:
                                                  ::   the exe and readme.txt only (no ROMs)
```

### Source layout

| Path | Contents |
|---|---|
| `src/core/` | Mega Drive hardware: 68000, Z80, VDP (with widescreen rendering: wider line buffer, 10-bit sprite X, per-line hooks), YM2612, SN76489, save states |
| `src/game/` | Per-game modules (`coi.cpp`, `qs.cpp`, `woi.cpp`): ROM patches, widescreen rules, HUD layout, edge masks, cheats, level select, sound lists; `common.cpp` applies patch lists |
| `src/app/` | Launcher, sessions, menus, music player / sound test, rewind, settings, ROM finder (`romfind.cpp`, `unzip.cpp`), PNG writer |
| `src/platform/` | Direct3D 11 video and DirectWrite UI text, XAudio2 audio, keyboard / XInput / DirectInput input |
| `src/tools/mdtool.cpp`, `tools/mdt.py` | Headless core as a DLL with a Python wrapper, used for all the reverse engineering and testing |
| `tools/m68dis.py` | 68000 disassembler (capstone) used during development |
| `tools/re/` | Reverse-engineering scripts behind the notes in `docs/` |
| `tools/trailer/` | Bot-played, headless trailer-footage pipeline |
| `tests/` | Unit tests for the CPU cores, sound chips and platform layer |
| `docs/` | Reverse-engineering notes per game (`re_*_game.md`: logic, levels, cheats, sound; `re_*_wide.md`: widescreen) and `TOOLS.md` |

### Technical notes

- **Patching.** Each game module lists ROM patches as `{address, original words, new words}`; applying a
  configuration restores the pristine ROM, applies the list for that width and cheat set and fixes the header
  checksum. A switch waits for a frame end where no changed word is at the 68000's PC or behind a return
  address on its stack (`patches_safe()` in `src/game/common.cpp`). Save states carry the configuration they
  ran with (`"qs|108|cheats"`), so a load restores that ROM first.
- **Widescreen rendering.** The VDP renders E extra pixels on each side (E = 20 / 40 / 64 / 108). The game
  logic runs unchanged; its windows are widened instead: sprite clipping, object despawn ranges, spawn
  triggers, camera clamps and animation windows. Per frame, each module decides from RAM whether the
  screen is gameplay (widened) or something else (pillarboxed 4:3), where the level edges are (masked),
  and remaps the HUD's sprites or scanlines to the screen edges.
- **Beyond the 512-pixel planes.** The Mega Drive's background layers are 512 pixels wide, so at 21:9
  (536 px) and in World of Illusion's shifted 2:1 view some columns can't come from VRAM. Those are drawn
  per scanline from the game's own level map in RAM/ROM, with the game's column writer's lookup (Castle of
  Illusion: the RAM block map, including Toyland's upside-down room; QuackShot: the chunk map; World of
  Illusion: layout → block → tile).
- **Castle of Illusion.** Tile streaming already keeps the whole 512×512 plane valid, so it needs no patch;
  sprites, objects, animated blocks and the camera clamp do. Widescreen applies in the gameplay states
  (`$F300` = `$0E` / `$10`); the HUD lines 192–223 are remapped. Boss arenas keep the game's locked camera.
- **QuackShot.** Objects are spawned from a grid of 128-pixel columns as the camera crosses them
  (`$19C08`). From 2:1 a second, farther column is processed each frame; entries placed to appear behind
  the player (swoop-in birds and others, 41 of 1,036) are held back until the camera reaches the spot they
  have at 4:3, so they show up where the original shows them. Walkers' turn-around limits are widened, and
  512-pixel rooms are centred at 21:9.
- **World of Illusion.** Plane streaming covers E ≤ 81 unpatched; sprites, despawn, spawn triggers, map
  animation, the 2-player keep-on-screen window and the auto-scroll window are widened. The original camera
  is kept and the picture slides near level edges instead, with a per-line plane B shift so parallax layers
  hold still.
- Full details, addresses and the scripts that verify them are in `docs/re_*.md`.

## Known limitations

- Widescreen: a few enemies only wake up once they reach the original 320-pixel screen area (they wait in
  the extra columns), and a handful of objects designed to appear just behind the player (some birds) pop
  in at the original screen's left edge. Attract-mode demos are recorded button presses, so in widescreen
  (where enemies appear a little earlier) they can go off course: at 2:1 and 21:9 two of QuackShot's five
  demos end with Donald falling or stuck. With widescreen off everything is exactly original.
- At 21:9: sprites entering at the very left edge can appear up to 20 px late, more enemies wait in the wider
  margins, and rooms narrower than the screen show bars: QuackShot's 512-pixel rooms (Dracula, the Ghosts,
  Pete, the final boss and a few others) have 12 pixels at each side; Castle of Illusion's boss arenas keep
  the locked camera, so the Evil Tree and the Library boss show bars of about 90 and 75 pixels a side. In
  Castle of Illusion, Toyland's animated blocks start moving a little after they come into view on the
  right. In QuackShot a defeated enemy occasionally doesn't return when you scroll back.
- World of Illusion keeps its original camera in widescreen and slides the picture near level edges: for the
  first stretch of a level (40 px at 16:9, 64 px at 2:1) the scenery holds still while the player walks.
  There, water and cloud layers keep their own motion, a slower background can show a little more of its
  left edge, message boxes can sit off-centre by up to the widescreen margin, and in 2-player mode the
  partner stops a little short of the screen edge.
- QuackShot's debug free movement, switched off inside a wall or the floor, leaves Donald stuck there (the
  game's own debug exit does the same); switch it off in open air.
- Pits and scripted deaths still cost a life with invincibility on (use infinite lives / tries too).
- Save states made by a newer version may not load in an older one.

## Credits and licences

- PC port by Darhkfox — [YouTube](https://www.youtube.com/adhgaming) ·
  [Reddit u/Darhkwing](https://www.reddit.com/user/Darhkwing) · [GitHub](https://github.com/Darhkfox).
- The port's own code is released under the MIT licence (`LICENSE`). It has no third-party code: the
  Mega Drive core is written from scratch. UI text uses the system's Segoe UI font through DirectWrite.
- *Castle of Illusion*, *QuackShot* and *World of Illusion* © 1990–1992 Sega. Mickey Mouse and Donald Duck
  © Disney. This project contains no ROM data. It is an unofficial fan project, not affiliated with or
  endorsed by Sega or Disney.
