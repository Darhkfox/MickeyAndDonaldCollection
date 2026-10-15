"""Shared helpers for the World of Illusion game-logic RE scripts (woi_game_*).

    import sys; sys.path.insert(0, r'K:\\projects\\castle of illusion\\tools\\re')
    from woi_game_util import *
"""
import os
import sys
import struct
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
STATES = os.path.join(TOOLS, 'states')
sys.path.insert(0, TOOLS)

from mdt import MD, WOI, BTN  # noqa: E402

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

SCRATCH = os.environ.get('WOI_SCRATCH', os.path.join(tempfile.gettempdir(), 'woi_game'))


def new_md():
    return MD(WOI)


def shot(md):
    """Return current frame as a PIL image."""
    w, h, px = md.pixels()
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


def grid(images, cols=4, labels=None, path=None, scale=1):
    """Paste PIL images into a grid (optionally with text labels) and save."""
    from PIL import ImageDraw
    w = max(i.size[0] for i in images)
    h = max(i.size[1] for i in images)
    lh = 12 if labels else 0
    rows = (len(images) + cols - 1) // cols
    g = Image.new('RGB', (w * cols, (h + lh) * rows), (40, 40, 40))
    d = ImageDraw.Draw(g)
    for n, im in enumerate(images):
        x = (n % cols) * w
        y = (n // cols) * (h + lh)
        g.paste(im, (x, y + lh))
        if labels:
            d.text((x + 2, y), str(labels[n]), fill=(255, 255, 0))
    if scale != 1:
        g = g.resize((g.size[0] * scale, g.size[1] * scale), Image.NEAREST)
    if path:
        g.save(path)
    return g


def snap(md):
    return bytes(md.ram)


def diff(a, b, lo=0, hi=0x10000):
    return [(0xFF0000 + i, a[i], b[i]) for i in range(lo, hi) if a[i] != b[i]]


def xref(target, game='woi', lo=0, hi=None):
    """Better xref search: tries every back offset, keeps decodes mentioning the address."""
    from m68dis import rom, _cs
    data = rom(game)
    hi = hi or len(data)
    t = target & 0xFFFFFF
    pats = [struct.pack('>I', t), struct.pack('>I', t | 0xFF000000)]
    txt = ['$%x' % t, '$%x' % (t | 0xFF000000)]
    if t >= 0xFF8000:
        pats.append(struct.pack('>H', t & 0xFFFF))
        txt.append('$%x.w' % (t & 0xFFFF))
    hits = {}
    for p in pats:
        start = lo
        while True:
            i = data.find(p, start, hi)
            if i < 0:
                break
            start = i + 1
            if i & 1:
                continue
            for back in range(2, 12, 2):
                s = i - back
                if s < 0:
                    continue
                ins = list(_cs.disasm(data[s:s + 16], s, 1))
                if ins and s + ins[0].size > i:
                    ops = ins[0].op_str
                    if any(x + ',' in ops + ',' or x + ')' in ops or x + '.l' in ops for x in txt):
                        hits[s] = ins[0].mnemonic + ' ' + ops
    return sorted(hits.items())


def dis(addr, n=30):
    from m68dis import dis as _dis
    return _dis('woi', addr, n)



# ---------------------------------------------------------------- navigation
MODE = 0xFFFFD0        # word: main-loop mode (bit 15 = handler "initialised" flag)


def mode(md):
    return md.ram16(MODE) & 0x7F


def pulse_until(md, cond, button='START', period=20, maxf=5000):
    """Tap `button` every `period` frames until cond(md) is true."""
    for f in range(maxf):
        md.hold(button if f % period < 3 else 0)
        md.run(1)
        if cond(md):
            md.hold(0)
            return True
    md.hold(0)
    return False


def to_board(md, settle=30):
    """Power-on -> options card board (mode $0D), ready for input (board sub-state $A850 == 1)."""
    md.reset()
    pulse_until(md, lambda m: md.ram16(MODE) == 0x800D)
    for _ in range(600):
        if md.ram16(0xFFA850) == 1:
            break
        md.run(1)
    md.run(settle)


if __name__ == '__main__':
    for a in sys.argv[1:]:
        print('==', a)
        for s, t in xref(int(a, 0)):
            print('  %06x  %s' % (s, t))
