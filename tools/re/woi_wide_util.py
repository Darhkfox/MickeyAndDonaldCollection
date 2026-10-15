r"""Shared helpers for the World of Illusion widescreen RE scripts (woi_wide_*).

    import sys; sys.path.insert(0, r'K:\projects\castle of illusion\tools\re')
    from woi_wide_util import *
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
STATES = os.path.join(TOOLS, 'states')
sys.path.insert(0, TOOLS)
# pin the documented analysis DLL (save states are only valid with the DLL that made them;
# a fresh build in build/tools would otherwise take precedence in mdt.py)
os.environ.setdefault('MDTOOL_DLL', os.path.join(TOOLS, 'bin', 'mdtool.dll'))

from mdt import MD, WOI, BTN  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
import numpy as np  # noqa: E402

SCRATCH = os.environ.get('WOI_WIDE_OUT', os.path.join(os.environ.get('TEMP', ROOT), 'woi_wide'))


def new_md(state=None):
    md = MD(WOI)
    if state:
        p = state if os.path.isabs(state) else os.path.join(STATES, state)
        md.load(open(p, 'rb').read())
    return md


def shot(md):
    """Current frame as a PIL RGB image."""
    w, h, px = md.pixels()
    a = np.array(px, dtype=np.uint32).reshape(h, w)
    rgb = np.stack([(a >> 16) & 255, (a >> 8) & 255, a & 255], axis=-1).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB')


def grid(images, cols=4, labels=None, path=None, scale=1, marks=None):
    """Paste PIL images into a grid (optional labels; marks = list of x columns to draw guides)."""
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
        if marks:
            for mx in marks:
                d.line([(x + mx, y + lh), (x + mx, y + lh + 3)], fill=(255, 0, 255))
                d.line([(x + mx, y + lh + h - 4), (x + mx, y + lh + h - 1)], fill=(255, 0, 255))
        if labels:
            d.text((x + 2, y), str(labels[n]), fill=(255, 255, 0))
    if scale != 1:
        g = g.resize((g.size[0] * scale, g.size[1] * scale), Image.NEAREST)
    if path:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        g.save(path)
    return g


def dis(addr, n=30, b=False):
    from m68dis import dis as _dis
    return _dis('woi', addr, n, show_bytes=b)


def rom_bytes():
    from m68dis import rom
    return rom('woi')


def w16(*words):
    return b''.join(struct.pack('>H', w & 0xFFFF) for w in words)


def xref(target, lo=0, hi=None):
    """Instructions mentioning an absolute address (abs.w for $FF8000+ or abs.l)."""
    from m68dis import rom, _cs
    data = rom('woi')
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
                    if any(x + ',' in ops + ',' or x + ')' in ops for x in txt):
                        hits[s] = ins[0].mnemonic + ' ' + ops
    return sorted(hits.items())


if __name__ == '__main__':
    for a in sys.argv[1:]:
        print('==', a)
        for s, t in xref(int(a, 0)):
            print('  %06x  %s' % (s, t))


# ---------------------------------------------------------------------------
# Game RAM (see docs/re_woi_wide.md)
CAMX, CAMY = 0xFF8510, 0xFF8538          # plane A camera (16.16; word = integer px)
BGX, BGY = 0xFF8570, 0xFF8598            # plane B camera
CAM_MINX, CAM_MAXX = 0xFF8528, 0xFF852C  # plane A camera X clamp (word = px)
CAM_MINY, CAM_MAXY = 0xFF8550, 0xFF8554
WORLD, ACT, ENTRY = 0xFFFFDA, 0xFFFFDC, 0xFFFFDE
MODE = 0xFFFFD8          # bit0: 1 = Mickey (1P) / P1 is Mickey, bit1: 2-player game
GSTATE = 0xFFFFD0        # main state: 1 init, 2 level load, 3 gameplay ...
VBMODE = 0xFFFFD2        # VBlank dispatch index (1 = gameplay)

OPTIONS_STATE = 'woi_wide_options.state'   # options card board, START starts the game


def goto(md, world, entry=0, mode=1, settle=150, state=OPTIONS_STATE):
    """Start stage (world 1..5, start entry $FFDE) from the options board state.
    mode = $FFD8 value: 0 Donald 1P, 1 Mickey 1P, 2/3 two players."""
    md.load(open(os.path.join(STATES, state), 'rb').read())
    md.hold('START')
    for i in range(200):
        md.run(1)
        if i == 4:
            md.hold(0)
        if md.ram16(GSTATE) == 2:
            break
    md.hold(0)
    md.set16(WORLD, world)
    md.set16(ENTRY, entry)
    md.set8(MODE, mode)
    for i in range(600):
        md.run(1)
        if md.ram16(GSTATE) == 3:
            break
    md.run(settle)
    return md


def cam(md):
    return md.ram16(CAMX), md.ram16(CAMY)


def drive_cam(md, tx, ty, maxf=2000, cb=None, carry=True, pulse=24):
    """Drive the plane-A camera to (tx, ty) through the game's own camera code: set the X/Y lock
    flags ($8524/$854C) and write the camera target; the game moves the camera <= 6 px/frame and
    clamps it, streaming as usual.  cb(md) is called every frame."""
    still = 0
    for i in range(maxf):
        md.set8(0xFF8524, 1); md.set8(0xFF854C, 1)
        md.set16(0xFF8518, tx); md.set16(0xFF851A, 0)
        md.set16(0xFF8540, ty); md.set16(0xFF8542, 0)
        if carry:   # keep the players inside the view so they don't die off-screen
            x, y = cam(md)
            for pa in (0xFFA000, 0xFFA080):
                if md.ram16(pa):
                    md.set16(pa + 0x10, x + 160); md.set16(pa + 0x14, y + 112)
                    md.set32(pa + 0x18, 0); md.set32(pa + 0x1C, 0)
                    md.set8(pa + 0x38, 5)     # hearts
        if pulse:   # tap A now and then to dismiss message boxes
            md.hold('A' if (md.frame % pulse) == 0 else 0)
        md.run(1)
        if cb:
            cb(md)
        x, y = cam(md)
        if abs(x - tx) <= 0 and abs(y - ty) <= 0:
            break
        if (x, y) == getattr(md, '_last_cam', None):
            still += 1
            if still > 20:
                break   # clamped
        else:
            still = 0
        md._last_cam = (x, y)
    return cam(md)


def mask_rule(md, E):
    """Runtime level-edge rule (doc section 4): pixels of the left / right extension to black out."""
    cx = md.ram16(CAMX)
    cx = cx - 0x10000 if cx & 0x8000 else cx
    lo, hi = md.ram16(CAM_MINX), md.ram16(CAM_MAXX)
    ml = max(0, min(E, E - (cx - lo)))
    mr = max(0, min(E, E - (hi - cx)))
    return ml, mr


def shot_masked(md, E, tint=True):
    """Screenshot with the mask rule applied (tint=True: red-tinted instead of black, for review)."""
    im = shot(md)
    if im.size[0] <= 320:
        return im
    ml, mr = mask_rule(md, E)
    a = np.array(im)
    W = a.shape[1]
    for x0, x1 in ((0, ml), (W - mr, W)):
        if x1 > x0:
            if tint:
                a[:, x0:x1] = (a[:, x0:x1] * 0.35 + np.array([120, 0, 0]) * 0.65).astype(np.uint8)
            else:
                a[:, x0:x1] = 0
    return Image.fromarray(a)
