r"""World of Illusion widescreen verification helpers.

PlaneChecker: every frame, samples the nametable cell shown at each 8-px screen position of the
(320+2E)-wide view for plane A (and optionally B), in *world* coordinates (screen x - hscroll(line),
screen y + vscroll).  Observations inside the original 320-px view are 'core', the others 'ext'.
An 'ext' observation that differs from a 'core' observation of the same world cell made within
`window` frames means the extension area showed stale / wrong tiles.

SpriteChecker: tracks object slots; reports objects that spawn or despawn while inside the
(320+2E) view (pop-in / pop-out), using the object X/Y and the camera.
"""
import numpy as np
from woi_wide_util import *  # noqa


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class PlaneChecker:
    def __init__(self, E=40, planes=('A',), window=90):
        self.E = E
        self.planes = planes
        self.window = window
        self.obs = {}           # key -> list of (frame, region, entry, sx, sy)

    def sample(self, md, xclip=None):
        """xclip = (lo, hi) world-x range of valid level for plane A (skip outside)."""
        v = np.frombuffer(bytes(md.vram), dtype='>u2')
        vs = md.vsram
        fr = md.frame
        regs = md.regs
        hbase = (regs[13] & 0x3F) << 10
        hs = v[hbase // 2: hbase // 2 + 448].reshape(224, 2).astype(np.int32)
        hmode = regs[11] & 3
        if hmode == 0:        # full screen
            hs = np.repeat(hs[:1], 224, axis=0)
        elif hmode == 2:      # per 8-line cell row (entries at every 8th line)
            hs = np.repeat(hs[::8], 8, axis=0)[:224]
        if regs[11] & 4:      # 2-cell column vscroll: not modelled -> skip frame
            return
        for p in self.planes:
            base = 0xC000 if p == 'A' else 0xE000
            col = 0 if p == 'A' else 1
            nt = v[base // 2: base // 2 + 64 * 32].reshape(32, 64)
            vsc = vs[col] & 0x3FF
            for sy in range(0, 224, 8):
                h = s16(int(hs[sy, col]))
                h = ((h + 512) % 1024) - 512 if abs(h) >= 1024 else h
                wy = sy + vsc
                for sx in range(-self.E, 320 + self.E, 8):
                    wx = sx - h
                    if p == 'A' and xclip and not (xclip[0] <= wx < xclip[1]):
                        continue
                    e = int(nt[(wy >> 3) & 31, (wx >> 3) & 63])
                    region = 'core' if 0 <= sx and sx + 8 <= 320 else 'ext'
                    # key uses full hscroll value so different bands stay separate
                    key = (p, wx >> 3, wy >> 3)
                    self.obs.setdefault(key, []).append((fr, region, e, sx, sy))

    def report(self):
        bad = []
        for key, lst in self.obs.items():
            core = [(f, e) for f, r, e, sx, sy in lst if r == 'core']
            if not core:
                continue
            cf = np.array([c[0] for c in core])
            for f, r, e, sx, sy in lst:
                if r != 'ext':
                    continue
                i = int(np.argmin(np.abs(cf - f)))
                if abs(int(cf[i]) - f) <= self.window and core[i][1] != e:
                    bad.append((f, key, sx, sy, e, core[i][1], int(cf[i]) - f))
        n_ext = sum(1 for lst in self.obs.values() for o in lst if o[1] == 'ext')
        return n_ext, bad


def objects(md):
    out = {}
    for base, n in [(0xFF9000, 48), (0xFFA100, 22), (0xFF9C00, 16)]:
        for i in range(n):
            a = base + 0x40 * i
            t = md.ram16(a)
            if t:
                out[a] = (t, s16(md.ram16(a + 0x10)), s16(md.ram16(a + 0x14)), md.ram8(a + 0xC), md.ram8(a + 0xD),
                          md.ram16(a + 0x2E))   # +$2E: placement record (spawned by $20AD4) or 0
    return out


class SpriteChecker:
    """Detect objects appearing/disappearing inside the visible (320+2E) view."""
    def __init__(self, E=40, margin=8, placed_only=True):
        self.E = E
        self.placed_only = placed_only
        self.margin = margin
        self.prev = None
        self.events = []

    def sample(self, md):
        cur = objects(md)
        allobj = cur
        if self.placed_only:
            cur = {a: o for a, o in cur.items() if o[5]}
        # objects becoming visible (bit2 of +$0D = 'not drawn' cleared) inside the wide view
        cxx, cyy = s16(md.ram16(CAMX)), s16(md.ram16(CAMY))
        pa = getattr(self, 'prev_all', None)
        hid = self.__dict__.setdefault('hidden_since', {})
        for a, o in allobj.items():
            if o[4] & 4:
                hid.setdefault((a, o[0]), md.frame)
        if pa is not None:
            for a, o in allobj.items():
                p = pa.get(a)
                since = hid.pop((a, o[0]), None) if not (o[4] & 4) else None
                if p and p[0] == o[0] and (p[4] & 4) and not (o[4] & 4) and since is not None                         and md.frame - since >= 12:   # ignore flicker
                    rx, ry = o[1] - cxx, o[2] - cyy
                    if -self.E + self.margin < rx < 320 + self.E - self.margin and 0 < ry < 224:
                        self.events.append((md.frame, 'unhide', a, o[0], rx, ry))
        self.prev_all = allobj
        cx, cy = s16(md.ram16(CAMX)), s16(md.ram16(CAMY))
        if self.prev is not None:
            for a, o in cur.items():
                p = self.prev.get(a)
                if p is None or p[0] != o[0]:
                    rx, ry = o[1] - cx, o[2] - cy
                    if -self.E + self.margin < rx < 320 + self.E - self.margin and 0 < ry < 224:
                        self.events.append((md.frame, 'spawn', a, o[0], rx, ry))
            for a, p in self.prev.items():
                o = cur.get(a)
                if o is None or o[0] != p[0]:
                    rx, ry = p[1] - self.pcx, p[2] - self.pcy
                    if -self.E + self.margin < rx < 320 + self.E - self.margin and 0 < ry < 224:
                        self.events.append((md.frame, 'despawn', a, p[0], rx, ry))
        self.prev = cur
        self.pcx, self.pcy = cx, cy
