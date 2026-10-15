"""Shared helpers for the QuackShot game-logic RE scripts (qs_game_*).

    import sys; sys.path.insert(0, r'K:\\projects\\castle of illusion\\tools\\re')
    from qs_game_common import *
    md = new_md()                 # power-on
    boot_to_map(md)               # SEGA -> title -> START -> intro skipped -> world map
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))           # tools/
from mdt import MD, QS  # noqa: E402

# ---- key RAM addresses (68000, $FFxxxx) ----
GAME_MODE = 0xFFFF18      # word: low byte = mode index, bit15 = "mode initialised" flag
VBL_MODE = 0xFFFF1A       # word: VBlank dispatch index
PAD_HELD = 0xFFFF8FB0     # byte: pad 1 held   (SACBRLDU: bit7 START ... bit0 UP)
PAD_PRESS = 0xFFFF8FB1    # byte: pad 1 newly pressed this frame
LOCATION = 0xFFFF8F34     # word: current location 1..11
ROOM = 0xFFFF8F36         # word: room index inside location (derived from entry)
STAGE_DBG = 0xFFFF8F40    # word: debug stage number 0..$2F ($2F = ending)
ENTRY = 0xFFFF8F74        # word: entry point index inside location
HEALTH = 0xFFFF8F96       # word: power (hearts/chilli meter) 0..5
LIVES = 0xFFFF8F98        # word
SCORE = 0xFFFFEF34        # long (binary)

MODE_NAMES = {
    0x00: 'SEGA logo', 0x01: 'title', 0x02: 'world map', 0x03: 'map->level',
    0x04: 'level load', 0x05: 'gameplay', 0x06: 'demo setup', 0x07: 'demo play',
    0x08: 'pause', 0x09: 'ending (1)', 0x0A: 'ending (2)', 0x0B: 'START/OPTIONS menu',
    0x0C: 'intro story', 0x0D: 'checksum error', 0x0E: 'new game init', 0x0F: 'game over',
    0x10: 'continue?',
}


def new_md():
    return MD(QS)


def mode(md):
    return md.ram8(0xFF19)


def run_until(md, cond, limit=3000, step=1):
    """Run frames until cond(md) is true. Returns frames run or -1."""
    n = 0
    while n < limit:
        if cond(md):
            return n
        md.run(step)
        n += step
    return -1


def wait_mode(md, m, limit=3000):
    return run_until(md, lambda d: mode(d) == m, limit)


def tap(md, btn, frames=2, after=2):
    md.press(btn, frames, after)


def boot_to_menu(md):
    """Power-on -> START/OPTIONS menu (mode $0B, initialised)."""
    assert wait_mode(md, 0x01) >= 0
    md.run(30)
    while mode(md) != 0x0B:
        tap(md, 'START', 2, 10)
    run_until(md, lambda d: d.ram16(GAME_MODE) & 0x8000, 200)
    md.run(60)


def boot_to_map(md, skip_intro=True):
    """Power-on -> world map (mode 2, map fully faded in, Duckburg selected)."""
    boot_to_menu(md)
    tap(md, 'START', 2, 10)          # choose START (cursor defaults to START)
    assert wait_mode(md, 0x0C, 300) >= 0
    if skip_intro:
        while mode(md) == 0x0C:
            tap(md, 'START', 2, 10)
    assert wait_mode(md, 0x02, 3000) >= 0
    run_until(md, lambda d: d.ram16(0xFFA344) == 1, 600)
    md.run(60)


def grid(paths, out, cols=4, labels=None, scale=1):
    """Compose screenshots into a labelled grid PNG."""
    from PIL import Image, ImageDraw
    ims = [Image.open(p) for p in paths]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    g = Image.new('RGB', (cols * w, rows * (h + 14)), (40, 40, 40))
    d = ImageDraw.Draw(g)
    for i, im in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * (h + 14)
        g.paste(im, (x, y + 14))
        if labels:
            d.text((x + 2, y + 1), labels[i], fill=(255, 255, 0))
    if scale != 1:
        g = g.resize((g.width * scale, g.height * scale))
    g.save(out)
    return out
