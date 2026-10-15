# Trailer tools

Builds `trailer_work/Mickey_and_Donald_Collection_PC_trailer.mp4` (1080p60, about 3.5 minutes) from footage
recorded with the real app. Same look as the Buster's Hidden Treasure / Buster Busts Loose trailers (their
recovered scripts are in `ref/`).

1. **Gameplay** - `python bot.py <clip> ...` (clips in `clips.py`). A headless search (mdtool) finds inputs that play
   the clip without hits, lost lives or falls, checks the plan by replaying it without state loads, and writes
   `trailer_work/cap/<clip>.state` (app save-state format) + `<clip>.pads` (uint16 p1, p2 per frame) + `<clip>.json`
   (RAM fingerprint after the clip).
2. **Recording** - `python record.py <clip> ...` runs the app hidden (`--autotest ... --capture-size 1920x1080`, one exe
   copy and settings.ini per recording in `trailer_work/inst/`), loads the state, replays the pads and records
   `<name>.mkv` (lossless, via ffmpeg) + `<name>.pcm`. The app's RAM fingerprint (`marks.txt`) must equal the bot's;
   clips with `takes` are recorded again with other settings (colour, filter, HUD). In fast forward the app runs
   3.995 frames per displayed frame, so its check point lands a frame or two before the bot's (same run).
3. **App screens and music** - `python ui.py <name> ...`: launcher, level select, cheats, save states, options,
   controls and the music tracks (recorded through the Music Player).
4. **Compositing** - `python trailer.py` (full encode), `python trailer.py preview [scene]`, `python trailer.py stills
   <scene> <n>`. Scenes, captions and the music medley (`MUSIC`) are in `timeline()`.

Helpers: `tb.py` (headless sessions that match the app), `sheets.py` (contact sheets of recordings).
App script commands used (autotest only): `record` / `stoprec`, `loadstate`, `pads`, `set key=value`, `mark`, `realtime`.
