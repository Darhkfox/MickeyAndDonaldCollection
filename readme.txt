================================================================================
  MICKEY & DONALD COLLECTION  --  PC Edition V1.0 - 15th Oct 26
================================================================================

  A custom PC port of three Sega Mega Drive / Genesis classics, with a
  launcher to pick between them:

     Castle of Illusion Starring Mickey Mouse ............ 1990, 1 player
     QuackShot Starring Donald Duck ...................... 1991, 1 player
     World of Illusion Starring Mickey Mouse and
       Donald Duck ....................................... 1992, 1-2 players

  The console hardware (68000 and Z80 CPUs, VDP graphics, YM2612 and PSG
  sound) is reproduced in native code, so the games run directly on Windows
  -- no separate emulator needed -- with a set of modern extras: true
  widescreen up to 21:9 ultrawide, fullscreen, filters and colour profiles,
  save states, fast forward and rewind, and an in-game menu (cheats, level
  select, music player and sound test).

  This is a personal, non-commercial project. It does NOT include the games
  themselves -- you must supply your own ROMs of cartridges you own.

  >>> IMPORTANT: this needs good dumps of the USA ROMs. <<<
  The port is built and tuned for these exact versions -- the widescreen
  mode, level select, cheats and other tweaks are matched to their
  internals. Other regions (European / Japanese), other revisions, bad
  dumps and hacks are NOT supported: the launcher tells you what the file
  it found is (for example "is the European version") instead of starting
  it. The ones you want are commonly named:

     Mickey Mouse - Castle of Illusion (U) [!].bin
        512 KB, CRC32 BA4E9FD0
     Quack Shot Starring Donald Duck (JUE) (REV 00) [!].bin
        512 KB, CRC32 88C8DD94   (the USA / World cartridge, first revision)
     World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin
        1024 KB (1 MB), CRC32 921EBD1C

  You only need the ROMs of the games you want to play; the others just
  show "ROM not found" in the launcher.

--------------------------------------------------------------------------------
  GETTING STARTED
--------------------------------------------------------------------------------

  1. Unzip the "Mickey and Donald Collection" folder anywhere you like.
  2. Put your ROMs in that folder, next to DisneyClassics.exe, or in a
     "roms" folder inside it.
  3. Double-click DisneyClassics.exe.

  Nothing needs installing. The first launch opens fullscreen; press F11 (or
  Alt+Enter) for a window.

  FINDING THE ROMS (automatic):
     The ROMs are recognised by their contents (CRC32), so the file names
     don't matter. These all work:
       - plain .bin / .md / .gen dumps
       - interleaved .smd dumps, and dumps with a 512-byte copier header
       - any of the above inside a .zip (no need to unzip). .7z and .rar
         are not read -- extract those first.
     The launcher looks next to the program, in its "roms" folder, and in
     the folder it was started from. ROMs you copy in while the launcher is
     open are picked up within a second or so.

  WRONG VERSION?
     If a file is another version of one of the games, its card in the
     launcher says "Wrong ROM version", names the file and says what it is:
     "the European version", "the Japanese version", "the REV 01 version",
     or "a modified copy or a bad dump". Swap it for the USA ROM listed
     above.


--------------------------------------------------------------------------------
  KEYBOARD CONTROLS  (player 1)
--------------------------------------------------------------------------------

     D-Pad .................. Arrow keys
     A ...................... Z
     B ...................... X
     C ...................... C
     Start .................. Enter

     Pause menu ............. Esc

  Every key can be changed under Options > Controls.


--------------------------------------------------------------------------------
  GAMEPAD CONTROLS  (Xbox-style layout; other USB pads work too)
--------------------------------------------------------------------------------

     D-Pad .................. D-Pad  or  left stick
     A ...................... X  (left button)
     B ...................... A  (bottom button)
     C ...................... B  (right button)
     Start .................. Start
     Pause menu ............. Back / View
     Fast forward ........... Right trigger (hold)
     Rewind ................. Left trigger (hold)

  This is the layout Sega's own Genesis collections use, so the main jump /
  action button sits at the bottom. Xbox-compatible pads (XInput) and other
  USB controllers (DirectInput) are supported, a pad plugged in while the
  game runs is picked up automatically, and every button can be changed
  under Options > Controls.


--------------------------------------------------------------------------------
  TWO PLAYERS  (World of Illusion)
--------------------------------------------------------------------------------

  World of Illusion can be played by two people at once. Player 1 uses the
  first connected gamepad plus the keyboard keys above. Player 2 uses the
  second gamepad, or these keyboard keys:

     D-Pad .................. I J K L
     A / B / C .............. U / O / P
     Start .................. Backspace

  So two people can share one keyboard, or use one pad plus the keyboard.
  Player 2's keys, buttons and pad are set under Options > Controls - Player
  2, or from the pause menu's "Player 2 Controls". The Device list names the
  connected controllers; Auto takes a pad the other player isn't using.

  Tip: tools such as DS4Windows or Steam Input can make one controller show
  up twice (a virtual Xbox pad plus the real one). If pressing one pad moves
  both players, pick each player's Device by hand.


--------------------------------------------------------------------------------
  HOTKEYS  (during play)
--------------------------------------------------------------------------------

     Esc ............ Pause menu
     F5 ............. Save state         (into the current slot)
     F9 ............. Load state         (from the current slot)
     Tab ............ Fast forward, 4x   (hold) -- can be switched off
     R .............. Rewind             (hold) -- up to the last minute of
                                         play; can be switched off
     F11 ............ Fullscreen / window (Alt+Enter works too)
     F12 ............ Screenshot         (saved in a "screenshots" folder)

  A key or pad button you bind to a game button keeps that job instead
  (for example, if you map a trigger to A, it no longer rewinds).


--------------------------------------------------------------------------------
  THE LAUNCHER
--------------------------------------------------------------------------------

  Each game has a card with a live attract-mode preview. Left / Right picks
  a game, Enter (or A on a pad) starts it. Below the cards:

     Options ........ The same settings as in the game (see OPTIONS below).
     Music Player ... The soundtracks of all three games (Left / Right
                      switches game).
     Credits ........ Where to find me: YouTube, Reddit and GitHub.
     Quit ........... Exit to the desktop.

  To start a game straight from a shortcut, make a shortcut to
  DisneyClassics.exe and add  --game coi,  --game qs  or  --game woi  to the
  end of its Target.


--------------------------------------------------------------------------------
  THE PAUSE MENU
--------------------------------------------------------------------------------

  Press Esc (keyboard) or Back / View (gamepad) during play. The game pauses
  while the menu is open. Up / Down moves, Left / Right changes a setting,
  Enter (A) selects, Esc (B) goes back. A line at the bottom explains the
  highlighted item.

  RESUME
     Carry on playing.

  SAVE STATES:  SLOT / SAVE STATE / LOAD STATE
     5 slots per game. Save State and Load State use the slot shown (the
     same as F5 and F9).

  LEVEL SELECT
     Start any level. Your current game is not kept, so make a save state
     first if you want to come back to it.
       Castle of Illusion ... 28 entries: every act of the Enchanted Forest,
                              Toyland, the Storm, the Library (including the
                              tea cups) and the Clock Tower, each boss, and
                              the final battle with Mizrabel.
       QuackShot ............ 48 areas: Duckburg, Mexico, Transylvania, the
                              Viking Ship, the South Pole, the Maharajah's
                              palace, Egypt, Pete's Hideout, the Island and
                              the ending. Donald gets the story items and
                              weapons he would have by that point.
       World of Illusion .... 38 stages across all five worlds. "Play As"
                              picks Mickey, Donald or 2 Players; stages
                              that aren't on that route are greyed out.

  CHEATS
     Switch each cheat on or off; they work straight away and stay on until
     you turn them off (even after you quit). "Disable All" turns them all
     off.
       Castle of Illusion ... Invincibility, Infinite Power, Infinite Tries,
                              Infinite Items, Infinite Continues, Start With
                              Full Power, and the developers' hidden level
                              select on the OPTIONS screen.
       QuackShot ............ Invincibility, Infinite Health, Infinite Lives,
                              Infinite Ammo, All Weapons (bubble gum and the
                              red and green plungers), Endless or Permanent
                              Quack Attack, and the developers' hidden free
                              movement (fly) mode.
       World of Illusion .... Invincibility, Infinite Hearts, Infinite Tries,
                              Maximum Hearts (both players).
     Pits and scripted deaths still cost a life with invincibility on --
     turn on infinite lives / tries as well if you want to be safe.

  SOUND TEST / MUSIC PLAYER
     Every music track, sound effect and voice of the game, played by the
     game's own sound driver, with meters for the sound channels.

  PLAYER 2 CONTROLS  (World of Illusion)
     The second player's keys and gamepad.

  OPTIONS, CREDITS
     See below.

  RESET GAME / RETURN TO LAUNCHER / QUIT TO DESKTOP
     Each asks you to confirm first.


--------------------------------------------------------------------------------
  OPTIONS
--------------------------------------------------------------------------------

  Every change takes effect straight away and is remembered for next time.
  "(default: ...)" is how each setting starts out.

  VIDEO
     Display Mode ...... Windowed or Fullscreen (borderless).
                                                 (default: Fullscreen)
     Monitor ........... Which display to use (listed by name). Dragging the
                         window to another screen picks it too.
     Window Size ....... 1x to 6x, in windowed mode.      (default: 3x)
     Widescreen ........ How wide the gameplay picture is:
                           Off (4:3) ......... the original picture
                           16:10 ............. 360 x 224
                           16:9 .............. 400 x 224
                           2:1 ............... 448 x 224
                           21:9 (ultrawide) .. 536 x 224, for 3440x1440 and
                                               2560x1080 screens
                         Castle of Illusion and QuackShot go up to 21:9,
                         World of Illusion up to 2:1 (set wider from another
                         game, it uses 2:1). Title screens, menus, maps and
                         cut-scenes stay 4:3.            (default: 16:9)
     Re-anchor HUD ..... In widescreen, moves the status bar items out to
                         the screen edges.               (default: ON)
     Pixel Aspect ...... Square pixels, CRT (4:3 pixels) or Stretch to
                         window.                    (default: Square pixels)
     Integer Scaling ... Scale by whole numbers only, for perfectly even
                         pixels.                         (default: OFF)
     Filter ............ Nearest (sharp), Sharp bilinear, Bilinear
                         (smooth), Scanlines or CRT.
                                                (default: Sharp bilinear)
     VSync ............. Sync to the monitor's refresh rate. (default: ON)
     Remove Sprite Flicker .. Lifts the console's limit on sprites per line,
                         so busy scenes don't flicker.   (default: ON)
     Show FPS .......... Frame rate in the corner.       (default: OFF)

  COLOUR
     Colour Profile .... Accurate (measured Mega Drive output levels),
                         Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia or
                         Handheld LCD.                   (default: Accurate)
     Brightness, Saturation, Gamma .. Fine adjustments. "Reset
                         Adjustments" puts them back to 0.

  AUDIO
     Volume ............ 0% to 100%.                     (default: 80%)
     Output Filter ..... The low-pass filter of the original console's audio
                         output: Off (raw), Model 1 (warm) or Model 2
                         (bright).                       (default: Model 1)
     FM Chip Distortion  The YM2612 "ladder effect" heard on original
                         Model 1 consoles.               (default: ON)

  CONTROLS  (and CONTROLS - PLAYER 2)
     Keyboard keys and gamepad buttons for each Mega Drive button. Highlight
     one, press Enter, then the new key or button (a key already in use
     swaps; Esc cancels). Also: Device (which pad), Left Stick Moves
     (default ON) and Reset to Defaults.

  SPEED
     Fast Forward ...... Hold Tab or the right trigger for 4x speed.
                                                         (default: ON)
     Rewind ............ Hold R or the left trigger to step back through the
                         last minute of play.            (default: ON)


--------------------------------------------------------------------------------
  SAVE STATES, SETTINGS AND SCREENSHOTS
--------------------------------------------------------------------------------

  Save states capture your exact position at any moment. There are 5 slots
  per game: pick one under Slot in the pause menu, then save with F5 and
  load with F9.

  Settings are kept in "settings.ini", save states in a "saves" folder and
  screenshots in a "screenshots" folder, all next to DisneyClassics.exe, so
  the whole folder is portable. If that folder can't be written to (for
  example under Program Files) they go to
  %LOCALAPPDATA%\Mickey and Donald Collection instead. Delete settings.ini
  to go back to the defaults.

  Save states made by this version may not load in an older one.


--------------------------------------------------------------------------------
  NOTES
--------------------------------------------------------------------------------

  WIDESCREEN
     Widescreen really shows more of each level: every game is patched so
     the extra columns hold real scenery, and enemies and items appear and
     move across the whole width. The camera stays inside the level where
     it can, the edges of the level are masked, and the HUD moves out to the
     screen edges. With widescreen off everything is exactly as the original.

     A few things are worth knowing:
       - Some enemies only wake up once they reach the original 4:3 screen
         area (they wait in the extra columns), and a handful of things meant
         to appear just behind the player (some birds in QuackShot) pop in
         at the edge of the original screen.
       - The attract-mode demos are recorded button presses, so in
         widescreen (where enemies appear a little earlier) they can play
         out differently: at 2:1 and 21:9 two of QuackShot's five demos end
         with Donald falling or stuck.
       - At 21:9, rooms narrower than the screen show bars: QuackShot's
         512-pixel boss rooms have 12 pixels at each side, and Castle of
         Illusion's Evil Tree and Library bosses keep the game's locked
         camera (bars of about 90 and 75 pixels a side).
       - World of Illusion keeps its original camera and slides the picture
         near the level edges instead: for the first stretch of a level the
         scenery holds still while you walk, then scrolls normally.

  QUACKSHOT'S FREE MOVEMENT CHEAT
     Switch it off in open air. Switched off inside a wall or the floor,
     Donald stays stuck there (the game's own debug exit does the same).


--------------------------------------------------------------------------------
  TROUBLESHOOTING
--------------------------------------------------------------------------------

  "Windows protected your PC" when starting it
     The program isn't signed, so Windows SmartScreen warns about it the
     first time. Click "More info", then "Run anyway".

  "ROM not found" on a card
     Check the file is the USA version listed at the top (the card shows the
     CRC32 it needs) and that it is next to DisneyClassics.exe or in the
     "roms" folder. Zipped is fine; .7z and .rar are not.

  "Wrong ROM version" on a card
     The card names the file it found and what it is. You need the USA ROM
     listed at the top.

  "Direct3D 11 could not be initialised"
     Update your graphics driver. The port needs a DirectX 11 capable GPU
     (anything from the last 10+ years).

  A gamepad doesn't work
     Check Options > Controls > Device: it lists the connected controllers.
     Pick yours by hand if Auto chooses the wrong one.


--------------------------------------------------------------------------------
  CREDITS
--------------------------------------------------------------------------------

  PC port by Darhkfox. Find me over at:

     YouTube ... https://www.youtube.com/adhgaming
     Reddit .... u/Darhkwing
     GitHub .... https://github.com/Darhkfox

  The port's own code is released under the MIT licence; the source is on
  GitHub.

  Castle of Illusion, QuackShot and World of Illusion (c) 1990-1992 Sega.
  Mickey Mouse and Donald Duck (c) Disney. This is an unofficial fan
  project, not affiliated with or endorsed by Sega or Disney, and it
  contains no ROM data.
