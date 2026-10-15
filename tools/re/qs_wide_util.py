r"""Shared helpers for the QuackShot widescreen RE scripts (qs_wide_*).

    import sys; sys.path.insert(0, r'K:\projects\castle of illusion\tools\re')
    from qs_wide_util import *
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
STATES = os.path.join(TOOLS, 'states')
sys.path.insert(0, TOOLS)
# Pin the reference core (tools/bin) unless MDTOOL_DLL is set: other agents may build
# build/tools/mdtool.dll, whose save states are not compatible with tools/states/*.state.
if not os.environ.get('MDTOOL_DLL') and os.path.exists(os.path.join(TOOLS, 'bin', 'mdtool.dll')):
    os.environ['MDTOOL_DLL'] = os.path.join(TOOLS, 'bin', 'mdtool.dll')

from mdt import MD, COI, QS, BTN  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

SCRATCH = os.environ.get('QS_WIDE_OUT', os.path.join(ROOT, 'build', 'qs_wide'))


def load_state(md, name):
    p = name if os.path.isabs(name) else os.path.join(STATES, name)
    md.load(open(p, 'rb').read())


def save_state(md, name):
    p = name if os.path.isabs(name) else os.path.join(STATES, name)
    open(p, 'wb').write(md.save())


def shot(md):
    """Return current frame as a PIL image."""
    w, h, px = md.pixels()
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


def grid(images, cols=4, labels=None, path=None, scale=1):
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
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        g.save(path)
    return g


def snap(md):
    return bytes(md.ram)


def diff(a, b, lo=0, hi=0x10000):
    return [(0xFF0000 + i, a[i], b[i]) for i in range(lo, hi) if a[i] != b[i]]


# ---------------------------------------------------------------------------
# QuackShot specifics (see docs/re_qs_wide.md)
LAYER_A = 0xFF8500      # plane A camera/layer struct
LAYER_B = 0xFF8540      # plane B camera/layer struct
LOC = 0xFF8F34          # location (0..9)
AREA = 0xFF8F74         # area within location
STATE = 0xFFFF18        # main-loop state (low byte = handler index, bit 15 = initialised)
VMODE = 0xFFFF1A        # VBlank handler index


def goto_area(md, loc, area, frames=200):
    """From any gameplay state: reload the game at location/area (state 4 = level load)."""
    md.set16(LOC, loc)
    md.set16(AREA, area)
    md.set16(STATE, 4)
    md.run(frames)


def layer(md, base):
    g = md.ram16
    return dict(xmax=g(base), xmin=g(base + 4), ymax=g(base + 8), ymin=g(base + 12),
                y=g(base + 0x20), x=g(base + 0x24), oy=g(base + 0x28), ox=g(base + 0x2C),
                chunks=md.ram32(base + 0x10), blocks=md.ram32(base + 0x14), mtiles=md.ram32(base + 0x18))


def _mapper(md, base):
    """Return f(x, y) -> nametable word the game's streaming code writes for world pixel (x, y)."""
    L = layer(md, base)
    ram = bytes(md.ram)
    rom = bytes(md.rom)
    def rd8(a):
        a &= 0xFFFFFF
        return ram[a & 0xFFFF] if a >= 0xFF0000 else rom[a]
    ch, bl, mtb = L['chunks'], L['blocks'], L['mtiles']
    def f(x, y):
        x &= 0xFFFF; y &= 0xFFFF
        idx = ((((y >> 8) << 5) + (x >> 8)) & 0xFFFF)
        if idx & 0x8000:
            idx -= 0x10000
        chunk = rd8(ch + idx)
        d5 = ((chunk << 8) + ((x >> 4) & 15) + (y & 0xF0)) & 0xFFFF
        if d5 & 0x8000:
            d5 -= 0x10000
        mt = rd8(bl + d5)
        off = mt * 8 + (4 if (y & 8) else 0) + (2 if (x & 8) else 0)
        return (rd8(mtb + off) << 8) | rd8(mtb + off + 1)
    return f


def expected_cells(md, base, x, y):
    return _mapper(md, base)(x, y)


def nametable_word(md, vbase, x, y):
    """Word in a 64x32 nametable at VRAM vbase for plane pixel (x, y)."""
    a = vbase + (((y >> 3) & 31) * 64 + ((x >> 3) & 63)) * 2
    v = md.vram
    return (v[a] << 8) | v[a + 1]


def check_plane(md, base, vbase, E, xr=None, yr=(0, 224), xlim=None, ylim=None, displayed=True):
    """Compare VRAM nametable against map data for the visible (extended) view.
    displayed=True uses the layer's previous-frame X/Y (+$2C/+$28), i.e. the scroll values the
    VBlank of the current frame wrote together with the queued tiles (call after md.run()).
    Returns sorted list of bad 8-px column offsets relative to camera X (-E..320+E)."""
    L = layer(md, base)
    if displayed == 'both':      # tolerate lag frames: pass if either the old or new X/Y matches
        a = check_plane(md, base, vbase, E, xr, yr, xlim, ylim, True)
        return a and check_plane(md, base, vbase, E, xr, yr, xlim, ylim, False)
    X, Y = (L['ox'], L['oy']) if displayed else (L['x'], L['y'])
    f = _mapper(md, base)
    vram = bytes(md.vram)
    if xr is None:
        xr = (-E, 320 + E)
    bad = set()
    for sx in range(xr[0] - (X & 7), xr[1], 8):
        wx = X + sx
        if xlim and not (xlim[0] <= wx < xlim[1]):
            continue
        for sy in range(yr[0] - (Y & 7), yr[1], 8):
            wy = Y + sy
            if ylim and not (ylim[0] <= wy < ylim[1]):
                continue
            a = vbase + (((wy >> 3) & 31) * 64 + ((wx >> 3) & 63)) * 2
            if f(wx, wy) != ((vram[a] << 8) | vram[a + 1]):
                bad.add(sx)
    return sorted(bad)


def cram_rgb(md, idx):
    c = md.cram[idx & 63]          # core stores CRAM as 9-bit BBBGGGRRR
    return ((c & 7) * 36, ((c >> 3) & 7) * 36, ((c >> 6) & 7) * 36)


def render_cells(md, words, cols, scale=4, bg=(255, 0, 255)):
    """Render a list of nametable words (row-major, `cols` per row) from VRAM to a PIL image."""
    rows = (len(words) + cols - 1) // cols
    im = Image.new('RGB', (cols * 8, rows * 8), bg)
    px = im.load()
    v = md.vram
    for n, w in enumerate(words):
        t = w & 0x7FF; pal = (w >> 13) & 3; hf = w & 0x800; vf = w & 0x1000
        ox = (n % cols) * 8; oy = (n // cols) * 8
        for y in range(8):
            for x in range(8):
                sx = 7 - x if hf else x; sy = 7 - y if vf else y
                b = v[t * 32 + sy * 4 + sx // 2]
                c = (b >> 4) if sx % 2 == 0 else (b & 15)
                if c:
                    px[ox + x, oy + y] = cram_rgb(md, pal * 16 + c)
    if scale != 1:
        im = im.resize((im.size[0] * scale, im.size[1] * scale), Image.NEAREST)
    return im


def window_words(md, row, c0=0, c1=40, base=0xD000):
    v = md.vram
    return [(v[base + row * 128 + c * 2] << 8) | v[base + row * 128 + c * 2 + 1] for c in range(c0, c1)]


def test_camera_hack(md):
    """TEST ONLY: make the camera velocity ($FF8534 X, $FF8530 Y, 16.16) persistent so a script
    can pan the camera with pan(): NOPs the per-frame clears at $2588/$258C.  Use together with
    the lock flags $FFEE56/$FFEE57 (non-zero = camera does not follow the player)."""
    md.patch16(0x2588, 0x4E71); md.patch16(0x258A, 0x4E71)
    md.patch16(0x258C, 0x4E71); md.patch16(0x258E, 0x4E71)


def pan(md, vx, vy, frames, cb=None):
    """Pan the camera (needs test_camera_hack) by vx, vy px/frame for `frames` frames."""
    for f in range(frames):
        md.set8(0xFFEE56, 1); md.set8(0xFFEE57, 1)
        md.set32(0xFF8534, int(vx * 65536) & 0xFFFFFFFF)
        md.set32(0xFF8530, int(vy * 65536) & 0xFFFFFFFF)
        md.run(1)
        if cb:
            cb(f)


def check_displayed(md, base, vbase, plane, E, lines=(0, 192), width=None):
    """Weaker, scroll-mode independent check using the VDP's actual scroll values (VSRAM and
    the H-scroll table at $D000, full-screen or per-line): every visible cell must equal the map
    at world column plane_x + 512*k for SOME k inside [0, width) (line-scrolled background bands
    rely on the map repeating every 512 px).  Returns list of (line, screen_x) failures."""
    f = _mapper(md, base)
    vram = bytes(md.vram)
    L = layer(md, base)
    if width is None:
        width = L['xmax'] + 320
    mode = md.regs[11] & 3
    vs = md.vsram[plane] & 0x3FF
    Y = L['oy']                               # world Y displayed (VSRAM holds Y & 0x3FF)
    bad = []
    for y in range(lines[0], lines[1], 4):
        hl = 0 if mode == 0 else (y & ~7 if mode == 2 else y)
        a = 0xD000 + hl * 4 + plane * 2
        h = ((vram[a] << 8) | vram[a + 1]) & 0x3FF
        py = (vs + y) & 255
        wy = Y + y
        for sx in range(-E, 320 + E, 8):
            px = (sx - h) & 511
            na = vbase + ((py >> 3) * 64 + (px >> 3)) * 2
            word = (vram[na] << 8) | vram[na + 1]
            ok = False
            for k in range(0, (width + 511) // 512 + 1):
                wx = px + 512 * k
                if wx >= width:
                    break
                if f(wx, wy) == word:
                    ok = True
                    break
            if not ok:
                bad.append((y, sx))
    return bad


def sat_pieces(md, base=0xFFF300):
    """Walk the RAM sprite table (DMA'd to VRAM $D400) along the link chain.
    Returns list of (slot, screen_x, screen_y, w_px, h_px, tile_word)."""
    out = []
    i = 0
    seen = set()
    while i not in seen and i < 80:
        seen.add(i)
        a = base + i * 8
        y = md.ram16(a) & 0x3FF
        sz = md.ram8(a + 2)
        link = md.ram8(a + 3) & 0x7F
        t = md.ram16(a + 4)
        x = md.ram16(a + 6) & 0x1FF
        out.append((i, x - 128, y - 128, ((sz >> 2) & 3) * 8 + 8, (sz & 3) * 8 + 8, t))
        if link == 0:
            break
        i = link
    return out


def mark_view(im, E, color=(255, 0, 0)):
    """Draw thin markers at the original 320-px view edges on an extended screenshot."""
    d = ImageDraw.Draw(im)
    for x in (E - 1, E + 320):
        for y in range(0, im.size[1], 4):
            d.point((x, y), fill=color)
    return im


# ---- HUD (window plane, lines 192..223) remap: output pixel column -> source pixel column ----
HUD_FILL_L = 78           # plain column: dark label strip on row 24, plain panel below
HUD_FILL_R = 237          # plain column: light panel on all rows (continues cols 26..29)


def hud_src_x(xo, E):
    """Source x (0..319, window plane) for output x (0..319+2E) of the status bar."""
    if xo < 79:
        return xo                       # left border + Power box + gap (window cols 0..9)
    if xo < 79 + E:
        return HUD_FILL_L               # filler (continues the Power/Temper label strip)
    if xo < 239 + E:
        return xo - E                   # Temper, Donald, weapon, item box (centred)
    if xo < 239 + 2 * E:
        return HUD_FILL_R               # filler (plain panel before the Points strip)
    return xo - 2 * E                   # Points + right border (window cols 30..39)


def hud_remap(im320, E, y0=192, y1=224):
    """Apply the remap to a 320-wide screenshot; returns a (320+2E)-wide image of lines y0..y1."""
    out = Image.new('RGB', (320 + 2 * E, y1 - y0))
    src = im320.load(); dst = out.load()
    for xo in range(320 + 2 * E):
        xs = hud_src_x(xo, E)
        for y in range(y0, y1):
            dst[xo, y - y0] = src[xs, y]
    return out
