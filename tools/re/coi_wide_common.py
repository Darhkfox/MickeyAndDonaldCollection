"""Shared helpers for the Castle of Illusion widescreen research scripts (coi_wide_*.py).

    import coi_wide_common as W
    md = W.new()                 # forest act 1 state
    W.goto_level(md, 0x05)       # jump to round $05 (sets $FF42, forces a reload via $F40F)
    W.apply_patches(md, 40)      # ROM patches for E=40 (see docs/re_coi_wide.md)
    md.set_ext(40, sprite_limit=False)
    W.fly_path(md, [(x, y), ...])   # "noclip" the player along a path, camera follows
    bad = W.check_planes(md, 40)    # compare visible nametable cells with the RAM block map
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
from mdt import MD, COI  # noqa: E402

STATES = os.path.join(TOOLS, 'states')

# ---------------------------------------------------------------- RAM map
CAM_A = 0xFFFA80      # camera / plane A struct ($40 bytes)
CAM_B = 0xFFFAC0      # plane B struct (same layout)
# struct offsets
C_Y, C_X, C_MAP, C_H, C_W, C_DEFS = 0x00, 0x04, 0x08, 0x0C, 0x0E, 0x10
C_PY, C_PX, C_DY, C_DX = 0x14, 0x18, 0x1C, 0x1E
C_MINY, C_MINX, C_MAXY, C_MAXX = 0x20, 0x22, 0x24, 0x26
ROUND = 0xFFFF42      # current round (level) index
MODE = 0xFFF300       # main state (word, jump-table offset); $0E = gameplay
VMODE = 0xFFF302      # VINT mode (1 = full-screen hscroll, 4 = per-line hscroll)
PLAYER = 0xFFDC00     # player object (+$10 X.l, +$14 Y.l, +$18 Xvel.l, +$1C Yvel.l)
PAUSE = 0xFFF41D

ROUND_NAMES = {
    0x00: 'Enchanted Forest 1', 0x02: 'Enchanted Forest 2', 0x03: 'Forest - spider web',
    0x01: 'Forest - tree climb', 0x04: 'Forest boss (tree)',
    0x05: 'Toyland 1 (tall)', 0x06: 'Toyland 2', 0x07: 'Toyland 3 (224px tall strip)',
    0x08: 'Toyland boss', 0x09: 'Storm/ruins 1', 0x0A: 'Storm/ruins 2', 0x0B: 'Storm/ruins 3',
    0x0C: 'Library 1', 0x0D: 'Library 2 (water)', 0x0E: 'Library 2b', 0x0F: 'Library 2c',
    0x10: 'Library 2d', 0x11: 'Library 2e', 0x12: 'Dessert door A', 0x13: 'Dessert door B',
    0x14: 'Dessert door C', 0x15: 'Dessert 1', 0x16: 'Dessert boss', 0x17: 'Castle 1',
    0x18: 'Castle 2 (water)', 0x19: 'Clock tower 1', 0x1A: 'Clock tower 2', 0x1B: 'Mizrabel',
}


def new(state='coi_forest.state'):
    md = MD(COI)
    load(md, state)
    return md


def load(md, state):
    p = state if os.path.isabs(state) else os.path.join(STATES, state)
    md.load(open(p, 'rb').read())


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def cam(md, base=CAM_A):
    r = md.ram16
    return dict(x=s16(r(base + C_X)), y=s16(r(base + C_Y)), w=r(base + C_W), h=r(base + C_H),
                minx=s16(r(base + C_MINX)), maxx=s16(r(base + C_MAXX)),
                miny=s16(r(base + C_MINY)), maxy=s16(r(base + C_MAXY)),
                dx=s16(r(base + C_DX)), dy=s16(r(base + C_DY)))


def goto_level(md, n, frames=240):
    """Reload into round n: $FF42 = n and raise the 'player died' flag $F40F.
    The main loop ($9834) then switches $F300 to $0C (round load, $95D2)."""
    md.set16(ROUND, n)
    md.set8(0xFFF40F, 1)
    md.run(frames)


def invuln(md):
    md.set16(0xFFF322, 5)    # power
    md.set8(0xFFF446, 3)     # hit-invulnerability timer (odd -> sprite stays visible)
    md.set8(0xFFF459, 1)     # skips the instant-death block test at $BD0E


def put_player(md, x, y):
    md.set32(PLAYER + 0x10, (x & 0xFFFF) << 16)
    md.set32(PLAYER + 0x14, (y & 0xFFFF) << 16)
    md.set32(PLAYER + 0x18, 0)
    md.set32(PLAYER + 0x1C, 0)


def fly_path(md, pts, speed=4, every=None, cb=None):
    """Move the player in straight segments between points (world px), 'speed' px per frame.
    cb(md, step) is called after each frame when step % every == 0."""
    x0 = s16(md.ram16(PLAYER + 0x10)); y0 = s16(md.ram16(PLAYER + 0x14))
    step = 0
    for (x1, y1) in pts:
        dx = x1 - x0; dy = y1 - y0
        n = max(1, int(max(abs(dx), abs(dy)) / speed))
        for i in range(1, n + 1):
            invuln(md)
            put_player(md, int(x0 + dx * i / n), int(y0 + dy * i / n))
            md.run(1)
            step += 1
            if cb and every and step % every == 0:
                cb(md, step)
        x0, y0 = x1, y1
    return step


def camera_driver(md, on=True):
    """TEST ONLY: replace 'jsr $F71A' (player-follow camera, $F6E4) by two NOPs so the camera
    speed $FA9E/$FA9C can be driven directly from Python (cam_path)."""
    md.patch16(0xF6E4, 0x4E71 if on else 0x4EBA)
    md.patch16(0xF6E6, 0x4E71 if on else 0x0034)


def cam_path(md, pts, speed=4, every=None, cb=None, keep_player=True):
    """Drive the camera (needs camera_driver(md, True)) through world camera positions.
    The game's own clamp ($F88E) still applies.  The player is parked at the view centre."""
    step = 0
    for (tx, ty) in pts:
        stuck = 0
        while True:
            c = cam(md)
            dx = max(-speed, min(speed, tx - c['x'])); dy = max(-speed, min(speed, ty - c['y']))
            if dx == 0 and dy == 0:
                break
            md.set16(CAM_A + C_DX, dx & 0xFFFF); md.set16(CAM_A + C_DY, dy & 0xFFFF)
            if keep_player:
                invuln(md)
                put_player(md, c['x'] + 160, c['y'] + 150)
            md.run(1)
            step += 1
            c2 = cam(md)
            stuck = stuck + 1 if (c2['x'], c2['y']) == (c['x'], c['y']) else 0
            if cb and every and step % every == 0:
                cb(md, step)
            if stuck > 8:
                break          # clamped
    md.set16(CAM_A + C_DX, 0); md.set16(CAM_A + C_DY, 0)
    return step


def hold_cam(md, frames, cb=None):
    """Run frames with the driven camera standing still (player parked at the view centre)."""
    for i in range(frames):
        c = cam(md)
        md.set16(CAM_A + C_DX, 0); md.set16(CAM_A + C_DY, 0)
        invuln(md)
        put_player(md, c['x'] + 160, c['y'] + 150)
        md.run(1)
        if cb:
            cb(md, i)


def layout_entries(md):
    """Decode the object layout at $CA00 (after round load): one dict per (cell, entry).
    Several cells may share a list (same entry address)."""
    r = md.ram8; r16 = md.ram16
    off = r16(0xFFCA00); w = r(0xFFCA02); h = r(0xFFCA03)
    a2 = 0xFFCA00 + off
    out = []
    for cy in range(h):
        for cx in range(w):
            li = r(0xFFCA04 + cy * w + cx)
            if not li:
                continue
            p = a2 + r16(a2 + li * 2 - 2)
            n = r(p) + 1; p += 1
            for i in range(n):
                out.append(dict(cx=cx, cy=cy, li=li, addr=p, fl=r(p), id=r(p + 1), sub=r(p + 2),
                                spawned=r(p + 3), y=s16(r16(p + 5)), x=s16(r16(p + 7))))
                p += 9
    return out


OBJ_SLOTS = [0xFFD000 + i * 0x40 for i in range(48)] + [0xFFDC40 + i * 0x40 for i in range(15)]


# ---------------------------------------------------------------- plane validator
def _vr16(v, a):
    a &= 0xFFFF
    return (v[a] << 8) | v[(a + 1) & 0xFFFF]


def check_plane(md, E, plane='A', lines=192, ignore_outside=True):
    """Compare every 8x8 cell of the given plane that is visible in the widened view
    (screen x in [-E, 320+E), lines 0..lines-1) with the tile the RAM block map says should
    be there.  Returns dict(center=n, left=n, right=n, outside=n, cells=[(sx, sy, got, exp)]).
    'outside' counts cells whose world position is outside the block map."""
    ram = bytes(md.ram); v = bytes(md.vram); regs = list(md.regs)
    base = CAM_A if plane == 'A' else CAM_B
    rw = lambda a: (ram[a & 0xFFFF] << 8) | ram[(a + 1) & 0xFFFF]
    rl = lambda a: (rw(a) << 16) | rw(a + 2)
    camx = s16(rw(base + C_X)); camy = s16(rw(base + C_Y))
    mapp = rl(base + C_MAP) & 0xFFFF; W = rw(base + C_W); H = rw(base + C_H)
    defs = rl(base + C_DEFS) & 0xFFFF
    nt = ((regs[2] & 0x38) << 10) if plane == 'A' else ((regs[4] & 7) << 13)
    hsb = (regs[13] & 0x3F) << 10
    hmode = regs[11] & 3
    vs = list(md.vsram)
    vsc = vs[0 if plane == 'A' else 1] & 0x3FF
    res = dict(center=0, left=0, right=0, outside=0, cells=[])
    for sy in range(0, lines, 8):
        if hmode == 3:
            ha = _vr16(v, hsb + sy * 4 + (0 if plane == 'A' else 2))
        elif hmode == 2:
            ha = _vr16(v, hsb + (sy & ~7) * 4 + (0 if plane == 'A' else 2))
        else:
            ha = _vr16(v, hsb + (0 if plane == 'A' else 2))
        ha &= 0x3FF
        # world x of screen x=0 (pick the congruent value closest to the camera)
        px0 = (-ha) & 0x1FF
        wx0 = camx + (((px0 - camx) + 256) & 0x1FF) - 256
        py = (sy + vsc) & 0x1FF
        wy = camy + (((py - (camy + sy)) + 256) & 0x1FF) - 256 + sy
        # align to cell grid: test cells whose top-left lies at plane cell boundaries
        for sx in range(-E - ((wx0 - E) & 7) % 8, 320 + E, 8):
            wx = wx0 + sx
            wxc = wx & ~7
            cx = (wxc >> 3) & 63; cy = (wy >> 3) & 63
            got = _vr16(v, nt + (cy * 64 + cx) * 2)
            bx = wxc >> 4; by = wy >> 4
            region = 'left' if sx + 8 <= 0 else ('right' if sx >= 320 else 'center')
            if not (0 <= bx < W and 0 <= by < H):
                res['outside'] += 1
                continue
            blk = ram[(mapp + by * W + bx) & 0xFFFF]
            exp = rw(defs + blk * 8 + ((wy >> 3) & 1) * 4 + ((wxc >> 3) & 1) * 2)
            if got != exp:
                res[region] += 1
                res['cells'].append((sx, sy, got, exp))
    return res


def check_planes(md, E):
    out = {'A': check_plane(md, E, 'A')}
    if not (md.ram8(0xFF9800) & 1):
        out['B'] = check_plane(md, E, 'B')
    return out


# ---------------------------------------------------------------- patches
def _w(x):
    return x & 0xFFFF


CAVE_H = 0x7FE80      # free space ($FF fill from $7FE6C to the end of the ROM)
CAVE_V = 0x7FE9A
CAVE_CAM = 0x7FEB0
CAVE_ANIM = 0x7FF40


def _words(b):
    return [struct.unpack('>H', b[i:i + 2])[0] for i in range(0, len(b), 2)]


def _asm(*ws):
    return b''.join(struct.pack('>H', w & 0xFFFF) for w in ws)


def spawner_cave_h(E):
    """jsr'd from $6D53C instead of 'move.w 4(a6),d0; move.w $18(a6),d3':
    d0/d3 = camera X / previous X shifted by +E (camera moving right) or -E (moving left)."""
    return _asm(0x323C, E,          # move.w #E,d1
                0x4A6E, 0x001E,     # tst.w  $1e(a6)       camera dX
                0x6A02,             # bpl.s  +2
                0x4441,             # neg.w  d1
                0x302E, 0x0004,     # move.w 4(a6),d0      X
                0xD041,             # add.w  d1,d0
                0x362E, 0x0018,     # move.w $18(a6),d3    previous X
                0xD641,             # add.w  d1,d3
                0x4E75)             # rts


def spawner_cave_v(E):
    """jsr'd from $6D586 instead of 'move.w 4(a6),d0; andi.w #$ff80,d0' (row spawn, first column)."""
    return _asm(0x302E, 0x0004,     # move.w 4(a6),d0
                0x0440, E,          # subi.w #E,d0
                0x0240, 0xFF80,     # andi.w #$ff80,d0
                0x4E75)             # rts


def anim_cave(E):
    """Visibility test for animated map blocks (replaces $F338-$F34B; in: d1 = block X, d2 = camX/16).
    Visible (carry clear) when  X+W+k >= cx  and  X+7 >= cx  and  X <= cx+20+k  and  X+W-25 <= cx,
    k = ceil(E/16).  The 2nd and 4th terms keep every written block inside the plane columns the
    streamer maintains ([cx-7, cx+24]); otherwise wide entries (Toyland 2 has W=5) wrap onto live
    columns on the other side of the plane."""
    k = (E + 15) // 16
    return _asm(0x3601,               # move.w d1,d3
                0xD668, 0x001C,       # add.w  $1c(a0),d3      X+W
                0x3003,               # move.w d3,d0
                0x5040 | ((k & 7) << 9),  # addq.w #k,d0
                0xB042,               # cmp.w  d2,d0
                0x6520,               # bcs.s  inv             X+W+k < cx
                0x3001,               # move.w d1,d0
                0x5E40,               # addq.w #7,d0
                0xB042,               # cmp.w  d2,d0
                0x6D18,               # blt.s  inv             X+7 < cx
                0x0443, 0x0019,       # subi.w #25,d3
                0xB642,               # cmp.w  d2,d3
                0x6E10,               # bgt.s  inv             X+W-25 > cx
                0x3002,               # move.w d2,d0
                0x0640, 0x14 + k,     # addi.w #20+k,d0
                0xB240,               # cmp.w  d0,d1
                0x6206,               # bhi.s  inv             X > cx+20+k
                0x023C, 0x00FE,       # andi.b #$fe,ccr        visible
                0x4E75,               # rts
                0x003C, 0x0001,       # inv: ori.b #1,ccr
                0x4E75)               # rts


def patches(E, groups=('sprites', 'objects', 'anim', 'sfx')):
    """ROM patches for an extension of E px on each side (0 < E <= 64).
    Returns a list of (addr, [original words], [new words], description)."""
    assert 0 < E <= 64
    k = (E + 15) // 16                     # blocks of 16 px
    up = max(0x1E0, 0x1C0 + E)
    P = []
    if 'sprites' in groups:
        # sprite piece X culling ($1C34): keep pieces whose left edge (VDP X) is in [$60-E, up)
        P.append((0x1C3C, [0x0C40, 0x0060], [0x0C40, _w(0x60 - E)], 'sprite piece cull: left bound $60 -> $60-E'))
        P.append((0x1C42, [0x0C40, 0x01E0], [0x0C40, up], 'sprite piece cull: right bound $1E0 -> max($1E0,$1C0+E)'))
        # plane-B attached background sprites (objects $BE/$BF, Dessert): show/hide window
        P.append((0xFE8E, [0x0C41, 0x0060], [0x0C41, _w(0x60 - E)], 'plane-B sprite objects: hide when X <= $60-E'))
        P.append((0xFE94, [0x0C41, 0x01E0], [0x0C41, up], 'plane-B sprite objects: hide when X >= up'))
        P.append((0xFEBA, [0x0641, 0x0018], [0x0641, 0x18 + E], 'plane-B sprite objects: appear E px earlier on the left'))
    if 'objects' in groups:
        # object delete window in the draw routine ($1B12): X-camX in [-128,512) -> [-128-E,512+E)
        P.append((0x1B12, [0x0C44, 0x0280, 0x6400, 0x017C, 0x4A44, 0x6B00, 0x0176],
                  [0x3004, 0x0640, E, 0x0C40, 0x280 + 2 * E, 0x6400, 0x0176],
                  'object delete window: move.w d4,d0; addi.w #E,d0; cmpi.w #$280+2E,d0; bcc.w $1C94'))
        # spawner: horizontal trigger/column use camX +E (moving right) / -E (moving left)
        P.append((0x6D53C, [0x302E, 0x0004, 0x362E, 0x0018], [0x4EB9, CAVE_H >> 16, CAVE_H & 0xFFFF, 0x4E71],
                  'spawner: jsr CAVE_H (shifted camera X for the column trigger); nop'))
        cw = _words(spawner_cave_h(E))
        P.append((CAVE_H, [0xFFFF] * len(cw), cw, 'CAVE_H code'))
        # spawner: row spawn covers 6 cell columns starting at (camX-E)/128-1
        P.append((0x6D586, [0x302E, 0x0004, 0x0240, 0xFF80], [0x4EB9, CAVE_V >> 16, CAVE_V & 0xFFFF, 0x4E71],
                  'spawner rows: jsr CAVE_V (first column from camX-E); nop'))
        cw = _words(spawner_cave_v(E))
        P.append((CAVE_V, [0xFFFF] * len(cw), cw, 'CAVE_V code'))
        P.append((0x6D59C, [0x7E04], [0x7E05], 'spawner rows: 6 columns (moveq #5,d7)'))
        # spawner initial fill ($6D768): columns from camX-E-$71 to camX+E+$18F
        P.append((0x6D772, [0x7E00], [0x7E00 | (_w(-E) & 0xFF)], 'spawner init: first column at camX-E (moveq #-E,d7)'))
        P.append((0x6D786, [0x0C47, 0x0280], [0x0C47, 0x280 + E], 'spawner init: loop while d7 < $280+E'))
    if 'anim' in groups:
        # animated map blocks ($9400 table): visibility test $F32E replaced by CAVE_ANIM (see doc 2.1)
        P.append((0xF338, [0x3601, 0xD668, 0x001C, 0xB642, 0x6500, 0x002C, 0x0642, 0x0014, 0xB242, 0x6222],
                  [0x4EB9, CAVE_ANIM >> 16, CAVE_ANIM & 0xFFFF, 0x6500, 0x002E, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71],
                  'anim-block visibility: jsr CAVE_ANIM; bcs.w $F36E; nop x5'))
        cw = _words(anim_cave(E))
        P.append((CAVE_ANIM, [0xFFFF] * len(cw), cw, 'CAVE_ANIM code'))
    if 'sfx' in groups:
        # "object on screen" test used only to decide whether to play an SFX ($1FF6)
        P.append((0x0200C, [0x2238, 0xFA84, 0xB2A8, 0x0010, 0x6410, 0x0681, 0x0140, 0x0000, 0xB2A8, 0x0010, 0x6504],
                  [0x2228, 0x0010, 0x92B8, 0xFA84, 0x0681, _w(E), 0x0000, 0x0C81, _w(320 + 2 * E), 0x0000, 0x6404],
                  'on-screen test ($1FF6, SFX only): camX-E <= X < camX+320+E'))
    return P


def camera_clamp_patches(E, plane_b=False):
    """Optional: keep the camera E px inside the level's original visible range (doc section 4).
    Hook at $EEEC (tail of camera-struct init $EEAC) -> code cave CAVE_CAM."""
    code = camera_cave_code(E, plane_b)
    words = _words(code)
    P = [(0xEEEC, [0x3029, 0x000E, 0xE948], [0x4EF9, CAVE_CAM >> 16, CAVE_CAM & 0xFFFF],
          'camera init: jmp CAVE_CAM (replaces move.w $e(a1),d0; lsl.w #4,d0)')]
    P.append((CAVE_CAM, [0xFFFF] * len(words), words, 'CAVE_CAM code'))
    # player X clamp ($F71A) uses minX+16: use the saved original minX (+$30 of the struct) instead
    P.append((0xF71E, [0x3238, 0xFAA2], [0x3238, 0xFAB0], 'player clamp: use original minX ($FAB0)'))
    return P


def camera_cave_code(E, plane_b=False):
    """Entry by jmp from $EEEC, a1 = camera struct.  Does what the replaced code did
    (maxX = W*16-320), saves the original minX/maxX at +$30/+$32 (unused bytes of the struct), then
    (plane A only unless plane_b) minX += E, maxX -= E; if the range collapses both become the
    centre (orig minX+maxX)/2; finally pulls the initial X (and previous X) into the new range."""
    body = [0x3029, 0x000E,          # move.w $e(a1),d0
            0xE948,                  # lsl.w #4,d0
            0x0440, 0x0140,          # subi.w #$140,d0
            0x3340, 0x0026,          # move.w d0,$26(a1)       maxX (original code)
            0x3340, 0x0032,          # move.w d0,$32(a1)       save original maxX
            0x3229, 0x0022,          # move.w $22(a1),d1
            0x3341, 0x0030]          # move.w d1,$30(a1)       save original minX
    adj = [0x0641, E,                # addi.w #E,d1
           0x0440, E,                # subi.w #E,d0
           0xB240,                   # cmp.w d0,d1
           0x6F0C,                   # ble.s store
           0x3229, 0x0030,           # move.w $30(a1),d1
           0xD269, 0x0032,           # add.w $32(a1),d1
           0xE241,                   # asr.w #1,d1
           0x3001,                   # move.w d1,d0
           0x3341, 0x0022,           # store: move.w d1,$22(a1)
           0x3340, 0x0026,           # move.w d0,$26(a1)
           0x3429, 0x0004,           # move.w 4(a1),d2
           0xB441,                   # cmp.w d1,d2
           0x6C02,                   # bge.s +2
           0x3401,                   # move.w d1,d2
           0xB440,                   # cmp.w d0,d2
           0x6F02,                   # ble.s +2
           0x3400,                   # move.w d0,d2
           0x3342, 0x0004,           # move.w d2,4(a1)
           0x3342, 0x0018]           # move.w d2,$18(a1)
    # plane B: re-sync its position to the (moved) plane A with the parallax routine $FB10 so the
    # first frame does not jump (a jump > 16 px would skip a streamed column)
    sync = [0x0C78, 0x0009, 0x9802,  # cmpi.w #9,$9802.w   (type 9 has side effects: skip)
            0x672A,                  # beq.s  done
            0x2F0E,                  # move.l a6,-(a7)
            0x4DF8, 0xFAC0,          # lea    $fac0.w,a6
            0x4EB9, 0x0000, 0xFB10,  # jsr    $fb10
            0x302E, 0x001E,          # move.w $1e(a6),d0
            0xD16E, 0x0004,          # add.w  d0,4(a6)
            0x302E, 0x001C,          # move.w $1c(a6),d0
            0xD156,                  # add.w  d0,(a6)
            0x3D6E, 0x0004, 0x0018,  # move.w 4(a6),$18(a6)
            0x3D56, 0x0014,          # move.w (a6),$14(a6)
            0x42AE, 0x001C,          # clr.l  $1c(a6)
            0x2C5F]                  # move.l (a7)+,a6
    if plane_b:
        body += adj + [0x4E75]
    else:
        body += [0xB2FC, 0xFA80,     # cmpa.w #$fa80,a1
                 0x6600 | (len(adj) * 2 + 2)]   # bne.s planeB
        body += adj + [0x4E75] + sync + [0x4E75]
    return _asm(*body)


def apply_patches(md, E, clamp=False, check=True, plane_b=False, fix_checksum=True):
    """Patch the loaded ROM copy (survives md.load()).  With fix_checksum the header word $18E is
    updated too, so the game also boots after md.reset() (boot code $31A-$342 verifies it)."""
    P = patches(E) + (camera_clamp_patches(E, plane_b) if clamp else [])
    rom = md.rom
    for addr, old, new, desc in P:
        cur = [(rom[addr + 2 * i] << 8) | rom[addr + 2 * i + 1] for i in range(len(old))]
        if cur == new:
            continue                      # already applied (ROM patches survive md.load())
        if check:
            assert cur == old, 'patch %06x mismatch: %s vs %s (%s)' % (addr, [hex(x) for x in cur], [hex(x) for x in old], desc)
        for i, word in enumerate(new):
            md.patch16(addr + 2 * i, word)
    if fix_checksum:
        md.patch16(0x18E, rom_checksum(bytes(md.rom)))
    return P


def rom_checksum(data):
    s = 0
    for i in range(0x200, len(data) - 1, 2):
        s = (s + ((data[i] << 8) | data[i + 1])) & 0xFFFF
    return s


def patched_rom(E, clamp=False, plane_b=False):
    data = bytearray(open(COI, 'rb').read())
    for addr, old, new, desc in patches(E) + (camera_clamp_patches(E, plane_b) if clamp else []):
        for i, word in enumerate(new):
            data[addr + 2 * i] = word >> 8
            data[addr + 2 * i + 1] = word & 0xFF
    cs = rom_checksum(data)
    data[0x18E] = cs >> 8; data[0x18F] = cs & 0xFF
    return bytes(data)


# ---------------------------------------------------------------- images
def shot(md):
    from PIL import Image
    w, h, px = md.pixels()
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


def strip(images, path, cols=1, gap=4, labels=None):
    from PIL import Image, ImageDraw
    if not images:
        return
    w = max(i.width for i in images); h = max(i.height for i in images)
    rows = (len(images) + cols - 1) // cols
    out = Image.new('RGB', (cols * (w + gap) - gap, rows * (h + gap) - gap), (255, 0, 255))
    d = ImageDraw.Draw(out)
    for n, im in enumerate(images):
        x = (n % cols) * (w + gap); y = (n // cols) * (h + gap)
        out.paste(im, (x, y))
        if labels:
            d.text((x + 2, y + 2), labels[n], fill=(255, 255, 0))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    out.save(path)


def hud_src_x(out_x, line, E):
    """HUD remap for lines 192..223 (doc section 5): output x in [0, 320+2E) -> source x in [0, 320)."""
    if line < 200 or line >= 216:                   # border rows 24 and 27: continuous 16-px pattern
        return 64 + ((out_x - E) % 16)
    if out_x < 64:
        return out_x                                # left margin + POWER (anchored left)
    if out_x < 96 + E:
        return 64 + ((out_x - 64) % 32)             # filler (blank columns 8..11)
    if out_x < 232 + E:
        return out_x - E                            # TRIES + ITEMS (centred)
    if out_x < 240 + 2 * E:
        return 64 + ((out_x - 232 - E) % 32)        # filler
    return out_x - 2 * E                            # SCORE + right margin (anchored right)


def hud_remap(im, E):
    """Apply hud_src_x to a widened screenshot (source = the original 320 columns at x E..E+319)."""
    im = im.copy()
    px = im.load()
    src = im.copy().load()
    for y in range(192, 224):
        for x in range(im.width):
            px[x, y] = src[E + hud_src_x(x, y, E), y]
    return im


def port_rules(md, E, clamp=True):
    """The per-frame rules a port applies (doc sections 4-6), computed from RAM/VDP state.
    Returns dict(wide, maskL, maskR, hud)."""
    st = md.ram16(MODE)
    wide = st in (0x0E, 0x10)
    mL = mR = 0
    if wide:
        camx = s16(md.ram16(CAM_A + C_X))
        lo = s16(md.ram16(0xFFFAB0 if clamp else CAM_A + C_MINX))
        hi = s16(md.ram16(0xFFFAB2 if clamp else CAM_A + C_MAXX))
        mL = max(0, min(E, lo + E - camx)); mR = max(0, min(E, camx + E - hi))
    return dict(wide=wide, maskL=mL, maskR=mR, hud=wide and md.regs[18] == 0x98)


def compose(md, E, clamp=True, im=None):
    """Screenshot post-processed like the port would show it: pillarbox outside gameplay, edge mask on
    lines 0..191, HUD remap on lines 192..223."""
    from PIL import ImageDraw
    im = shot(md) if im is None else im
    r = port_rules(md, E, clamp)
    if not r['wide']:
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, E - 1, im.height - 1), fill=(0, 0, 0))
        d.rectangle((E + 320, 0, im.width - 1, im.height - 1), fill=(0, 0, 0))
        return im
    if r['hud']:
        im = hud_remap(im, E)
    d = ImageDraw.Draw(im)
    if r['maskL']:
        d.rectangle((0, 0, r['maskL'] - 1, 191), fill=(0, 0, 0))
    if r['maskR']:
        d.rectangle((im.width - r['maskR'], 0, im.width - 1, 191), fill=(0, 0, 0))
    return im


def mark_edges(im, E):
    """Draw thin markers at the original 320 px borders (x=E and x=E+320) on a copy."""
    from PIL import ImageDraw
    im = im.copy()
    d = ImageDraw.Draw(im)
    for x in (E - 1, E + 320):
        for y in range(0, im.height, 4):
            d.point((x, y), fill=(255, 0, 255))
    return im
