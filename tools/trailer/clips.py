"""Gameplay clips for the trailer (searched by bot.py, recorded by record.py).

name: game, level (level-select index), var (World of Illusion: 0 Mickey, 1 Donald, 2 two players),
      ext (extra columns per side: 0 4:3, 20 16:10, 40 16:9, 64 2:1, 108 21:9), n (recorded frames),
      pre (frames played before recording starts), dir ('right' / 'left'), intro (moves before the search),
      switch (frame, ext): widescreen changes live at that frame (a multiple of 20),
      cheats, fly (QuackShot free movement: flying moves), ff (recorded holding fast forward, 4 game
      frames per recorded frame), rewind (frames, loops): after `frames` of play R is held for `loops`,
      takes: extra recordings of the same run with other settings {suffix: {setting: value}}
"""
CLIPS = {
    # ---- the three games
    'coi_forest':   dict(game='coi', level=0, ext=40, n=600, pre=60),
    'qs_duckburg':  dict(game='qs', level=0, ext=40, n=600, pre=60),
    'woi_forest2':  dict(game='woi', level=0, var=2, ext=40, n=600, pre=60),
    # ---- Castle of Illusion
    'coi_wide':     dict(game='coi', level=5, ext=0, switch=(200, 40), n=480, pre=40),
    'coi_ultra':    dict(game='coi', level=21, ext=108, n=540, pre=60),
    'coi_storm':    dict(game='coi', level=9, ext=40, n=420, pre=60),
    'coi_library':  dict(game='coi', level=13, ext=40, n=420, pre=60),
    'coi_clock':    dict(game='coi', level=25, ext=40, n=420, pre=60),
    'coi_treetops': dict(game='coi', level=3, ext=40, n=420, pre=60),
    'coi_soldiers': dict(game='coi', level=7, ext=40, n=420, pre=60),
    'coi_castlehall': dict(game='coi', level=23, ext=40, n=420, pre=60),
    # ---- QuackShot
    'qs_ultra':     dict(game='qs', level=3, ext=108, n=540, pre=60, takes={'hud0': {'hud_anchor': 0}}),
    'qs_fly':       dict(game='qs', level=7, ext=40, n=420, pre=30, cheats=['free_move'], fly=True),
    'qs_egypt':     dict(game='qs', level=32, ext=40, n=420, pre=60),
    'qs_viking':    dict(game='qs', level=14, ext=40, n=420, pre=60),
    'qs_south':     dict(game='qs', level=19, ext=40, n=420, pre=60),
    'qs_palace':    dict(game='qs', level=23, ext=40, n=420, pre=60),
    'qs_rooftops':  dict(game='qs', level=1, ext=108, n=420, pre=60),
    'qs_hideout':   dict(game='qs', level=38, ext=108, n=420, pre=60),
    # ---- World of Illusion
    'woi_wide':     dict(game='woi', level=26, var=0, ext=64, n=480, pre=60),
    'woi_carpet':   dict(game='woi', level=6, var=2, ext=40, n=480, pre=60),
    'woi_sea':      dict(game='woi', level=12, var=0, ext=64, n=420, pre=60),
    'woi_garden':   dict(game='woi', level=30, var=2, ext=40, n=420, pre=60),
    'woi_toybox':   dict(game='woi', level=25, var=2, ext=40, n=420, pre=60),
    'woi_sunny':    dict(game='woi', level=2, var=1, ext=64, n=420, pre=60),
    'woi_ship':     dict(game='woi', level=19, var=2, ext=40, n=420, pre=60),
    # ---- features shared by all three
    'col':          dict(game='woi', level=26, var=0, ext=40, n=420, pre=90,
                         takes={'vivid': {'color': 2}, 'warm': {'color': 3}, 'lcd': {'color': 7}}),
    'filt':         dict(game='coi', level=0, ext=40, n=420, pre=300,
                         takes={'nearest': {'filter': 0}, 'scan': {'filter': 3}, 'crt': {'filter': 4}}),
    'ff':           dict(game='qs', level=0, ext=40, n=240, pre=30, ff=True),
    'rew':          dict(game='coi', level=10, ext=40, n=480, pre=60, rewind=(300, 180)),
}
