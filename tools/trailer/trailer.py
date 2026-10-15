"""Mickey & Donald Collection trailer: app footage (trailer_work/cap, 1920x1080 lossless) -> 1080p60 mp4.
Same look as the Buster's Hidden Treasure / Buster Busts Loose PC trailers (blurred backdrop, lower-third
captions, orange chips, PLUS cards, montage, end card), with a medley of the three games' music.

Every scene is (frame count, render(i)), so lengths are known up front and any frame can be previewed:
  python trailer.py                          full encode
  python trailer.py preview [scene ...]      middle still of each scene
  python trailer.py stills scene n           n evenly spaced stills of a scene
"""
import os, sys, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')), 'trailer_work')
CAP = os.path.join(WORK, 'cap')
OUT_MP4 = os.path.join(WORK, 'Mickey_and_Donald_Collection_PC_trailer.mp4')
FPS = 60
CAP_FPS = 53693175.0 / (3420.0 * 262.0)      # the recordings run at the console's 59.92 Hz
VW, VH = 1920, 1080
ORANGE = (245, 130, 32)
WHITE = (255, 255, 255)
SUBC = (205, 214, 230)
F_BOLD = r'C:\Windows\Fonts\segoeuib.ttf'
F_REG = r'C:\Windows\Fonts\segoeui.ttf'
S = lambda sec: int(round(sec * FPS))
GAME_NAME = {'coi': ('CASTLE OF ILLUSION', (196, 150, 255)), 'qs': ('QUACKSHOT', (255, 200, 70)),
             'woi': ('WORLD OF ILLUSION', (110, 200, 255))}

_fonts = {}
def font(path, size):
    if (path, size) not in _fonts: _fonts[(path, size)] = ImageFont.truetype(path, size)
    return _fonts[(path, size)]


# ---------------------------------------------------------------- footage
class Clip:
    """frames of a recording, decoded by ffmpeg on demand (sequential reads are cheap, a jump back reopens)"""
    def __init__(self, name):
        self.path = os.path.join(CAP, name + '.mkv')
        w, h, n, _ = open(os.path.join(CAP, name + '.txt')).read().split()
        self.w, self.h, self.n = int(w), int(h), int(n)
        self.p = None; self.pos = 0; self.last = None
    def __len__(self): return self.n
    def _open(self, k):
        if self.p: self.p.kill()
        self.p = subprocess.Popen(['ffmpeg', '-v', 'error', '-ss', '%.4f' % (k / CAP_FPS), '-i', self.path,
                                   '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        self.pos = k
    def __getitem__(self, k):
        k = max(0, min(k, self.n - 1))
        if self.last is not None and k == self.pos - 1: return self.last
        if self.p is None or k < self.pos or k > self.pos + 120: self._open(k)
        size = self.w * self.h * 3
        while self.pos <= k:
            buf = self.p.stdout.read(size)
            if len(buf) < size: break
            self.last = np.frombuffer(buf, np.uint8).reshape(self.h, self.w, 3); self.pos += 1
        return self.last

_clips = {}
def clip(name):
    if name not in _clips: _clips[name] = Clip(name)
    return _clips[name]


# ------------------------------------------------------------ frame ops
def game_rect(ext):
    """where the app draws the game picture in a 1920x1080 window (square pixels, fit)"""
    gw, gh = 320 + 2 * ext, 224
    s = min(VW / gw, VH / gh)
    w, h = gw * s, gh * s
    x, y = int((VW - w) // 2), int((VH - h) // 2)
    return x, y, int(w), int(h)


def backdrop(rgb, rect=None):
    """blurred, darkened full-frame fill made from the picture"""
    if rect: x, y, w, h = rect; rgb = rgb[y:y + h, x:x + w]
    im = Image.fromarray(np.ascontiguousarray(rgb)).resize((96, 54), Image.BILINEAR).filter(ImageFilter.GaussianBlur(2))
    a = np.asarray(im.resize((VW, VH), Image.BILINEAR)).astype(np.uint16)
    return (a * 45 // 100).astype(np.uint8)


TOP_21 = 20          # 21:9 pictures sit at the top, the caption goes in the space below


def canvas(rgb, ext=40):
    """a recorded gameplay frame; the bars around a 4:3 / 2:1 / 21:9 picture get the blurred backdrop"""
    if ext == 40: return rgb.copy()
    r = game_rect(ext)
    x, y, w, h = r
    f = backdrop(rgb, r)
    ty = TOP_21 if ext == 108 else y
    f[ty:ty + h, x:x + w] = rgb[y:y + h, x:x + w]
    return f


UI_SCALE = 0.85

def ui_canvas(rgb):
    """an app screen, shown a little smaller over its own blurred backdrop (room for the caption)"""
    w, h = int(VW * UI_SCALE), int(VH * UI_SCALE)
    small = np.asarray(Image.fromarray(rgb).resize((w, h), Image.LANCZOS))
    f = backdrop(rgb)
    x, y = (VW - w) // 2, 24
    f[y:y + h, x:x + w] = small
    return f


def cap_bottom(game, ext):
    """caption bottom edge: clear of Castle of Illusion's and QuackShot's status bar (bottom of the picture)"""
    if game is None or ext == 108: return 1052
    return 900 if game in ('coi', 'qs') else 1000


def fade(frame, a):
    return (frame.astype(np.uint16) * int(max(0, min(1, a)) * 256) >> 8).astype(np.uint8)


def dim(frame, pct):
    return (frame.astype(np.uint16) * pct // 100).astype(np.uint8)


# ---------------------------------------------------------------- text
def ease(t):
    t = max(0.0, min(1.0, t)); return 1 - (1 - t) ** 3

_layer_cache = {}
def caption_layer(head, sub, kicker=None):
    """lower-third box: optional kicker (game name in its colour), heading, sub-lines"""
    key = (head, sub, kicker)
    if key in _layer_cache: return _layer_cache[key]
    fk, fh, fs = font(F_BOLD, 28), font(F_BOLD, 56), font(F_REG, 32)
    d0 = ImageDraw.Draw(Image.new('RGBA', (1, 1)))
    subs = sub.split('\n') if sub else []
    hw = d0.textbbox((0, 0), head, font=fh)[2]
    sw = max([d0.textbbox((0, 0), t, font=fs)[2] for t in subs] or [0])
    kw = d0.textbbox((0, 0), kicker[0], font=fk)[2] if kicker else 0
    top = 40 if kicker else 0
    w = max(hw, sw, kw) + 90; h = 100 + 42 * len(subs) + top
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 14, fill=(8, 12, 24, 205))
    d.rectangle((0, 14, 9, h - 15), fill=ORANGE + (255,))
    if kicker: d.text((42, 14), kicker[0], font=fk, fill=kicker[1] + (255,))
    d.text((40, 14 + top), head, font=fh, fill=WHITE + (255,))
    for k, t in enumerate(subs): d.text((42, 86 + top + 42 * k), t, font=fs, fill=SUBC + (255,))
    _layer_cache[key] = im
    return im


def put_caption(frame, head, sub, t_in, t_out, kicker=None, bottom=948):
    """t_in: frames since the caption started, t_out: frames until it ends"""
    a = ease(t_in / 14) * ease(t_out / 14)
    if a <= 0: return frame
    layer = caption_layer(head, sub, kicker)
    dx = int((1 - ease(t_in / 14)) * -40)
    base = Image.fromarray(frame).convert('RGBA')
    lay = layer
    if a < 1:
        al = np.asarray(lay).copy(); al[..., 3] = (al[..., 3] * a).astype(np.uint8)
        lay = Image.fromarray(al)
    base.alpha_composite(lay, (150 + dx, bottom - lay.size[1]))
    return np.asarray(base.convert('RGB')).copy()


def centered_text(frame, lines, alpha=1.0):
    """lines: [(text, font_path, size, colour, y)]"""
    base = Image.fromarray(frame).convert('RGBA')
    lay = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for text, fp, size, col, y in lines:
        f = font(fp, size)
        w = d.textbbox((0, 0), text, font=f)[2]
        d.text(((VW - w) // 2 + 3, y + 4), text, font=f, fill=(0, 0, 0, 160))
        d.text(((VW - w) // 2, y), text, font=f, fill=col + (255,))
    if alpha < 1:
        al = np.asarray(lay).copy(); al[..., 3] = (al[..., 3] * alpha).astype(np.uint8)
        lay = Image.fromarray(al)
    base.alpha_composite(lay)
    return np.asarray(base.convert('RGB')).copy()


def label(f, text, x=None, y=110, size=40, anchor='left', alpha=1.0):
    """orange chip; x=None -> top-right corner; anchor: which edge x refers to"""
    if alpha <= 0: return f
    base = Image.fromarray(f).convert('RGBA')
    lay = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    fnt = font(F_BOLD, size)
    w = d.textbbox((0, 0), text, font=fnt)[2]
    if x is None: x = VW - 150 - w - 30
    elif anchor == 'center': x -= w // 2
    elif anchor == 'right': x -= w
    d.rounded_rectangle((x - 18, y - 8, x + w + 18, y + size + 16), 10, fill=(8, 12, 24, 215))
    d.text((x, y), text, font=fnt, fill=ORANGE + (255,))
    if alpha < 1:
        al = np.asarray(lay).copy(); al[..., 3] = (al[..., 3] * alpha).astype(np.uint8)
        lay = Image.fromarray(al)
    base.alpha_composite(lay)
    return np.asarray(base.convert('RGB')).copy()


def caption(f, head, sub, i, n, game=None, ext=40, kicker=True, ui=False):
    if not head: return f
    k = GAME_NAME[game] if (game and kicker) else None
    return put_caption(f, head, sub, i, n - 1 - i, k, cap_bottom(None if ui else game, ext))


# ---------------------------------------------------------------- scenes
def plain(name, a, n, head=None, sub=None, ext=40, step=1, chip=None, game=None, chips=None, kicker=True, chip_y=110):
    """chips: [(from_frame, to_frame, text)] timed chips (top right)"""
    def render(i):
        f = canvas(clip(name)[a + i * step], ext)
        if chip: f = label(f, chip, y=chip_y)
        for c0, c1, text in chips or []:
            if c0 <= i < c1: f = label(f, text, y=chip_y, alpha=ease((i - c0) / 10) * ease((c1 - 1 - i) / 10))
        return caption(f, head, sub, i, n, game, ext, kicker)
    return n, render


def ui(name, a, n, head=None, sub=None, game=None):
    """an app screen recording"""
    def render(i):
        return caption(ui_canvas(clip(name)[a + i]), head, sub, i, n, game, ui=True)
    return n, render


def scene_title(n, name='woi_forest2', a=60):
    def render(i):
        f = dim(clip(name)[a + i], 35)
        f = fade(f, ease(i / S(1.0)))
        ta = ease((i - S(0.6)) / S(0.8))
        f = centered_text(f, [('THREE SEGA MEGA DRIVE CLASSICS', F_BOLD, 46, ORANGE, 330),
                              ('MICKEY & DONALD COLLECTION', F_BOLD, 104, WHITE, 392),
                              ('NOW ON PC', F_BOLD, 64, WHITE, 540)], ta)
        if i > n - S(0.5): f = fade(f, (n - i) / S(0.5))
        return f
    return n, render


def scene_wide(name, a, n, switch, head, sub, game):
    """4:3 -> 16:9 live: at the switch the picture opens out over 24 frames"""
    narrow = game_rect(0)
    def render(i):
        k = a + i - switch
        rgb = clip(name)[a + i]
        if k < 0:
            f = canvas(rgb, 0)
            f = label(f, '4:3', x=VW // 2, y=130, size=44, anchor='center')
        else:
            f = rgb.copy()
            if k < 24:
                m = int((1 - ease(k / 24)) * (VW - narrow[2]) / 2)
                bg = backdrop(rgb)
                f[:, :m] = bg[:, :m]; f[:, VW - m:] = bg[:, VW - m:]
            if k < S(2.5): f = label(f, '16:9', x=VW // 2, y=130, size=44, anchor='center')
        return caption(f, head, sub, i, n, game)
    return n, render


def scene_before_after(a_name, b_name, a, n, cut, head, sub, chip_a, chip_b, ext=40, game=None):
    """the same run recorded twice: first part from a_name, then b_name"""
    def render(i):
        before = i < cut
        f = canvas(clip(a_name if before else b_name)[a + i], ext)
        y = TOP_21 + 40 if ext == 108 else 130
        f = label(f, chip_a if before else chip_b, x=VW // 2, y=y, size=44, anchor='center')
        return caption(f, head, sub, i, n, game, ext)
    return n, render


def scene_colour(base, others, a, per, game=None):
    """split screen: the left part from `base`, the right from each of `others` in turn"""
    n = per * len(others)
    def render(i):
        other, name_ = others[i // per]
        j = i % per
        f = clip(base)[a + i].copy()
        g = clip(other)[a + i]
        split = VW // 2 + int(np.sin(j / per * np.pi * 2) * 300)
        f[:, split:] = g[:, split:]
        f[:, split - 2:split + 2] = 255
        f = label(f, 'ACCURATE', x=split - 34, y=250, anchor='right')
        f = label(f, name_, x=split + 34, y=250)
        return caption(f, 'COLOUR PROFILES',
                       'Accurate, Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia and Handheld LCD\n'
                       'Plus brightness, saturation and gamma', i, n, game, kicker=False)
    return n, render


def scene_filters(names, a, per, region, game='coi'):
    """the same run with each filter; a region of the 1080p picture shown at 2x so the filter shows"""
    x0, y0, w, h = region
    n = per * len(names)
    def render(i):
        name, chip = names[i // per]
        rgb = clip(name)[a + i]
        crop = rgb[y0:y0 + h, x0:x0 + w]
        img = np.repeat(np.repeat(crop, 2, axis=0), 2, axis=1)
        f = backdrop(rgb)
        ox, oy = (VW - img.shape[1]) // 2, 0
        f[oy:oy + img.shape[0], ox:ox + img.shape[1]] = img
        f = label(f, chip)
        return caption(f, 'FILTERS', 'Nearest, sharp bilinear or smooth scaling, scanlines and a CRT look', i, n,
                       None, kicker=False)
    return n, render


def scene_features(clips, per, items, heading='PLUS'):
    n = per * len(clips)
    def render(i):
        name, a, ext = clips[i // per]
        f = canvas(clip(name)[a + i % per], ext)
        f = dim(f, 50)
        lines = [(heading, F_BOLD, 50, ORANGE, 250)]
        for k, t in enumerate(items):
            if i >= S(0.5) + k * S(0.8):
                lines.append((t, F_BOLD, 52, WHITE, 340 + k * 92))
        return centered_text(f, lines, ease(i / S(0.4)) * ease((n - 1 - i) / S(0.4)))
    return n, render


def scene_montage(clips, per, head, sub):
    n = per * len(clips)
    def render(i):
        name, a, ext, game = clips[i // per]
        f = canvas(clip(name)[a + i % per], ext)
        return put_caption(f, head, sub, i, n - 1 - i, None, 1000)
    return n, render


def scene_end(n, name='woi_forest2'):
    def render(i):
        c = clip(name)
        f = dim(backdrop(c[300 + (i % 300)]), 60)
        f = centered_text(f, [
            ('MICKEY & DONALD COLLECTION', F_BOLD, 90, WHITE, 180),
            ('PC PORT  \u2022  V1.0', F_BOLD, 52, ORANGE, 310),
            ('github.com/Darhkfox', F_BOLD, 50, WHITE, 420),
            ('Requires your own ROMs of the USA releases \u2014 not included', F_REG, 38, SUBC, 505),
            ('Castle of Illusion (USA)   \u2022   QuackShot (USA / World, REV 00)   \u2022   World of Illusion (USA)',
             F_REG, 30, SUBC, 560),
            ('youtube.com/adhgaming   \u2022   u/Darhkwing', F_REG, 38, SUBC, 660),
            ('Fan project. Not affiliated with Disney or SEGA.', F_REG, 28, (150, 160, 180), 900),
        ], ease(i / S(0.8)))
        if i > n - S(1.2): f = fade(f, (n - i) / S(1.2))
        return f
    return n, render


# ---------------------------------------------------------------- timeline
def timeline():
    return [
        # ---- opening: the collection, the launcher, the three games
        ('title',      scene_title(S(6))),
        ('launcher',   ui('ui_launcher', 0, S(13), 'ONE COLLECTION, THREE CLASSICS',
                          'Castle of Illusion, QuackShot and World of Illusion\nA launcher with live previews of all three')),
        ('coi',        plain('coi_forest', 0, S(8), 'CASTLE OF ILLUSION',
                             'Starring Mickey Mouse  \u2022  1990\nInto the witch Mizrabel\'s castle to rescue Minnie',
                             game='coi', kicker=False)),
        ('qs',         plain('qs_duckburg', 0, S(8), 'QUACKSHOT',
                             'Starring Donald Duck  \u2022  1991\nA treasure hunt around the world, armed with a plunger gun',
                             game='qs', kicker=False)),
        ('woi',        plain('woi_forest2', 0, S(8), 'WORLD OF ILLUSION',
                             'Starring Mickey Mouse & Donald Duck  \u2022  1992\nPlay as either of them \u2014 or team up with a friend',
                             game='woi', kicker=False)),
        # ---- Castle of Illusion
        ('coi_wide',   scene_wide('coi_wide', 60, S(8), 140, 'WIDESCREEN',
                                  '16:10, 16:9, 2:1 or 21:9 \u2014 switch any time\nEvery level patched so the extra space shows real level content',
                                  game='coi')),
        ('coi_ultra',  plain('coi_ultra', 90, S(7.5), 'ULTRAWIDE 21:9',
                             'Dessert Land across a 21:9 screen\nScenery, enemies and props fill the whole width',
                             ext=108, game='coi')),
        ('coi_ls',     ui('ls_coi', 20, S(7), 'LEVEL SELECT',
                          'All 28 levels, straight from the pause menu\nPlus the developers\' own hidden level select', game='coi')),
        ('coi_cheats', ui('ch_coi', 40, S(5), 'CHEATS', 'Invincibility, infinite power, tries and items', game='coi')),
        # ---- QuackShot
        ('qs_hud',     scene_before_after('qs_ultra_hud0', 'qs_ultra', 0, S(9), S(4), 'HUD RE-ANCHORED',
                                          'The status bar moves out to the edges of a wide screen\nOr keep it centred \u2014 your choice',
                                          'CENTRED', 'AT THE EDGES', ext=108, game='qs')),
        ('qs_fly',     plain('qs_fly', 0, S(6), 'FREE MOVEMENT',
                             'QuackShot\'s hidden developer fly mode, now a cheat', game='qs')),
        ('qs_ls',      ui('ls_qs', 20, S(8), 'LEVEL SELECT',
                          'All 48 areas, with the story items and plungers you would have by then', game='qs')),
        ('qs_world',   plain('qs_egypt', 0, S(6), 'MORE OF THE WORLD',
                             'Enemies come in from the edges of the wider view\nThe original timing, enemies and difficulty stay the same',
                             game='qs')),
        # ---- World of Illusion
        ('woi_2p',     plain('woi_toybox', 0, S(7), 'TWO PLAYERS',
                             'A second gamepad, or two people on one keyboard\nPads are shared out automatically', game='woi')),
        ('woi_ctl',    ui('ctl_woi', 30, S(4), 'EVERY BUTTON REBINDABLE', 'Keyboard and gamepad, for both players', game='woi')),
        ('woi_wide',   plain('woi_wide', 0, S(8), '2:1 WIDESCREEN',
                             'World of Illusion goes up to 2:1\nScenery beyond the console\'s own layers drawn from the level map',
                             ext=64, game='woi')),
        ('woi_ls',     ui('ls_woi', 20, S(7), 'LEVEL SELECT', 'All 38 stages as Mickey, as Donald or together', game='woi')),
        # ---- features in all three
        ('colour',     scene_colour('col', [('col_vivid', 'VIVID'), ('col_warm', 'WARM CRT'), ('col_lcd', 'HANDHELD')], 0, S(3),
                                    game='woi')),
        ('filters',    scene_filters([('filt_nearest', 'NEAREST'), ('filt', 'SHARP BILINEAR'), ('filt_scan', 'SCANLINES'),
                                      ('filt_crt', 'CRT')], 0, S(1.6), (420, 360, 960, 540))),
        ('options',    ui('opt_video', 60, S(8), 'CHANGE IT LIVE',
                          'Widescreen, filters and colours switch instantly from the menu')),
        ('ff',         plain('ff', 0, S(4), 'FAST FORWARD', 'Hold Tab or the right trigger', chip='FAST FORWARD  4\u00d7',
                             game='qs', kicker=False)),
        ('rewind',     plain('rew', 180, S(7), 'REWIND', 'Hold R or the left trigger to step back through the last minute',
                             chips=[(S(2), S(7), 'REWIND')], game='coi', kicker=False)),
        ('states',     ui('st_qs', 40, S(5), 'SAVE STATES', 'Five slots per game \u2014 F5 and F9, or from the pause menu')),
        ('music',      ui('mus_woi_title', 20, S(7), 'MUSIC PLAYER & SOUND TEST',
                          'Every track, effect and voice from all three games\nPlayed by each game\'s own sound driver')),
        ('plus',       scene_features([('coi_storm', 60, 40), ('qs_south', 60, 40), ('woi_toybox', 120, 40)], S(2.6),
                                      ['Xbox, PlayStation and other USB pads',
                                       'Borderless fullscreen or windowed, on any monitor',
                                       'Integer scaling and CRT pixel aspect',
                                       'No more sprite flicker',
                                       'Every setting remembered'])),
        ('and',        scene_features([('coi_clock', 60, 40), ('woi_sea', 60, 64)], S(2.6),
                                      ['ROMs found automatically \u2014 any file name',
                                       'Title screens, maps and cut-scenes kept in 4:3',
                                       'Screenshots with F12, fullscreen with F11'], 'AND')),
        ('montage',    scene_montage([('coi_soldiers', 150, 40, 'coi'), ('qs_palace', 150, 40, 'qs'),
                                      ('woi_garden', 150, 40, 'woi'), ('coi_castlehall', 150, 40, 'coi'),
                                      ('qs_viking', 150, 40, 'qs'), ('woi_sunny', 150, 64, 'woi'),
                                      ('woi_ship', 150, 40, 'woi'), ('qs_hideout', 150, 108, 'qs')], S(1.75),
                                     'THREE COMPLETE GAMES', 'From the Enchanted Forest to the Magician\'s Castle')),
        ('end',        scene_end(S(10))),
    ]


# music: (first scene of the section, recording, start offset in seconds)
MUSIC = [('title', 'mus_woi_title', 0.0), ('coi_wide', 'mus_coi_forest', 0.0), ('qs_hud', 'mus_qs_duckburg', 0.0),
         ('woi_2p', 'mus_woi_forest', 0.0), ('colour', 'mus_coi_title', 0.0), ('plus', 'mus_woi_sky', 0.0)]
XFADE = 1.2


def load_pcm(name, start):
    a = np.fromfile(os.path.join(CAP, name + '.pcm'), np.int16).reshape(-1, 2).astype(np.float32)
    loud = np.nonzero(np.abs(a).max(1) > 300)[0]
    a = a[max(0, (loud[0] if len(loud) else 0) - 200):]
    return a[int(start * 48000):]


def make_music_bed(scenes):
    starts, t = {}, 0
    for name, (n, _) in scenes:
        starts[name] = t; t += n
    total = int(t / FPS * 48000)
    bed = np.zeros((total, 2), np.float32)
    xf = int(XFADE * 48000)
    for k, (scene, rec, off) in enumerate(MUSIC):
        s0 = int(starts[scene] / FPS * 48000)
        s1 = int(starts[MUSIC[k + 1][0]] / FPS * 48000) if k + 1 < len(MUSIC) else total
        a = load_pcm(rec, off)
        a0 = max(0, s0 - xf // 2)          # each track starts half a crossfade early and runs half a crossfade on
        a1 = min(total, s1 + xf // 2)
        seg = a[:a1 - a0]
        if len(seg) < a1 - a0: seg = np.concatenate([seg, np.zeros((a1 - a0 - len(seg), 2), np.float32)])
        env = np.ones(len(seg), np.float32)
        if k > 0: env[:xf] = np.linspace(0, 1, xf)
        if k + 1 < len(MUSIC): env[-xf:] = np.linspace(1, 0, xf)
        bed[a0:a1] += seg * env[:, None]
    fi, fo = int(0.3 * 48000), int(3.0 * 48000)
    bed[:fi] *= np.linspace(0, 1, fi)[:, None]
    bed[-fo:] *= np.linspace(1, 0, fo)[:, None]
    out = os.path.join(WORK, 'music_bed.pcm')
    np.clip(bed * 1.1, -32768, 32767).astype(np.int16).tofile(out)
    return out


def build():
    scenes = timeline()
    total = sum(n for _, (n, _) in scenes)
    print('total %d frames = %.1f s' % (total, total / FPS), flush=True)
    music = make_music_bed(scenes)
    ff = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error',
        '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{VW}x{VH}', '-r', str(FPS), '-i', '-',
        '-f', 's16le', '-ar', '48000', '-ac', '2', '-i', music,
        '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-preset', 'slow', '-crf', '17',
        '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '192k', '-shortest', '-movflags', '+faststart',
        OUT_MP4], stdin=subprocess.PIPE)
    done = 0
    for k, (name, (n, render)) in enumerate(scenes):
        for i in range(n):
            ff.stdin.write(np.ascontiguousarray(render(i)[:, :, :3]).tobytes())
        done += n
        print(f'scene {k:2d} {name:10s}: {n:5d} frames  (t={done / FPS:6.1f}s)', flush=True)
    ff.stdin.close(); ff.wait()
    print('wrote', OUT_MP4, f'{done / FPS:.1f}s')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] in ('preview', 'stills'):
        out = os.path.join(WORK, 'preview'); os.makedirs(out, exist_ok=True)
        scenes = dict(timeline())
        if sys.argv[1] == 'preview':
            for name in (sys.argv[2:] or list(scenes)):
                n, render = scenes[name]
                Image.fromarray(render(n // 2)).save(os.path.join(out, name + '.png')); print(name, n, flush=True)
        else:
            name, cnt = sys.argv[2], int(sys.argv[3])
            n, render = scenes[name]
            for j in range(cnt):
                i = int((j + 0.5) * n / cnt)
                Image.fromarray(render(i)).save(os.path.join(out, '%s_%02d.png' % (name, j)))
            print(name, n)
    else:
        build()
