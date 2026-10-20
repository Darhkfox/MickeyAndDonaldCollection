"""Mickey & Donald Collection v1.1 update trailer: the same look as the v1.0 trailer (trailer.py's helpers), new
footage from v11_rec.py. Focus: achievements, Castle of Illusion widescreen all the way through (boss fights, the
castle hallway), MD+ soundtracks (Castle of Illusion and QuackShot, the Music Player), the new cheats, rebindable
hotkeys; QuackShot and World of Illusion briefly.

  python trailer_v11.py                          full encode -> trailer_work/Mickey_and_Donald_Collection_v1.1_update.mp4
  python trailer_v11.py preview [scene ...]      middle still of each scene
  python trailer_v11.py stills scene n           n evenly spaced stills of a scene

Sound: a medley of the games' own music recorded through the Music Player, except where a scene's own sound is the
point (the MD+ soundtrack scenes: their recording's audio, arranged music and the game's sound effects).
"""
import os, sys, subprocess
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import trailer as T  # noqa: E402
from trailer import (S, FPS, CAP, WORK, VW, VH, F_BOLD, F_REG, ORANGE, WHITE, SUBC, clip, canvas, ui_canvas,  # noqa: E402
                     caption, label, centered_text, fade, dim, ease, backdrop, put_caption, plain, ui,
                     scene_features, TOP_21)

OUT_MP4 = os.path.join(WORK, 'Mickey_and_Donald_Collection_v1.1_update.mp4')
CAP_FPS = T.CAP_FPS


# ---------------------------------------------------------------- scenes
def scene_title(n, name='v11_hall4', a=300):
    def render(i):
        f = dim(canvas(clip(name)[a + i], 108), 35)
        f = fade(f, ease(i / S(1.0)))
        ta = ease((i - S(0.5)) / S(0.8))
        f = centered_text(f, [('MICKEY & DONALD COLLECTION', F_BOLD, 96, WHITE, 330),
                              ('VERSION 1.1', F_BOLD, 78, ORANGE, 460),
                              ('EXCITING NEW FEATURES AND UPDATES!', F_BOLD, 52, WHITE, 575)], ta)
        if i > n - S(0.5): f = fade(f, (n - i) / S(0.5))
        return f
    return n, render


def scene_card(n, lines, name, a, ext=108):
    """a short text card over dimmed footage"""
    def render(i):
        f = dim(canvas(clip(name)[a + i], ext), 40)
        al = ease(i / S(0.4)) * ease((n - 1 - i) / S(0.4))
        return centered_text(f, lines, al)
    return n, render


def scene_ba(a_name, b_name, a, n, cut, head, sub, chip_a, chip_b, ext=108, game='coi', b_off=0):
    """before / after: the same moment from two recordings (b_off: b_name's frames start later)"""
    def render(i):
        before = i < cut
        f = canvas(clip(a_name if before else b_name)[a + i + (0 if before else b_off)], ext)
        y = TOP_21 + 40 if ext == 108 else 130
        f = label(f, chip_a if before else chip_b, x=VW // 2, y=y, size=44, anchor='center')
        return caption(f, head, sub, i, n, game, ext)
    return n, render


def scene_seq(parts, head, sub, game='coi', chips=None):
    """parts: [(recording, first frame, frames, ext)] played one after another under one caption; chips: one per part"""
    n = sum(p[2] for p in parts)
    def render(i):
        j = i
        for k, (name, a, m, ext) in enumerate(parts):
            if j < m: break
            j -= m
        f = canvas(clip(name)[a + j], ext)
        if chips and chips[k]:
            f = label(f, chips[k], y=TOP_21 + 40 if ext == 108 else 130, alpha=ease(j / 8) * ease((m - 1 - j) / 8))
        return caption(f, head, sub, i, n, game, ext)
    return n, render


def scene_popup(name, a, n, head, sub):
    """a 21:9 recording shown whole: the app's achievement pop-up sits at the top of the window, over the bar"""
    def render(i):
        rgb = clip(name)[a + i]
        x, y, w, h = T.game_rect(108)
        f = backdrop(rgb, (x, y, w, h))
        f[y:y + h] = rgb[y:y + h]
        top = rgb[:y]
        m = top.max(axis=2) > 12
        f[:y][m] = top[m]
        return caption(f, head, sub, i, n, None, kicker=False)
    return n, render


def ui_segments(name, segs, head, sub, game=None):
    """an app screen recording, cut: segs = [(first frame, frames)] shown one after another (MD_MP_SEGS)"""
    n = sum(m for _, m in segs)
    def render(i):
        j = i
        for a, m in segs:
            if j < m: break
            j -= m
        return caption(ui_canvas(clip(name)[a + j]), head, sub, i, n, game, ui=True)
    return n, render


# the Music Player: the arranged title, Tab to the original, then a boss theme only the pack has
MD_MP_SEGS = [(25, 250), (318, 190), (866, 230)]


class PhoneClip:
    """the Android recording (screenrecord on the emulator, 2400x1080, converted to 60 fps at the phone frame's
    screen size: trailer_work/cap/android_phone_60.mkv, from 1.0 s of android_phone.mp4); sequential reads"""
    W, H = 1520, 684
    def __init__(self, path):
        self.path = path; self.p = None; self.pos = 0; self.last = None
    def _open(self, k):
        if self.p: self.p.kill()
        self.p = subprocess.Popen(['ffmpeg', '-v', 'error', '-ss', '%.4f' % (k / 60.0), '-i', self.path, '-f', 'rawvideo',
                                   '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.pos = k
    def __getitem__(self, k):
        if self.last is not None and k == self.pos - 1: return self.last
        if self.p is None or k < self.pos or k > self.pos + 120: self._open(k)
        size = self.W * self.H * 3
        while self.pos <= k:
            buf = self.p.stdout.read(size)
            if len(buf) < size: break
            self.last = np.frombuffer(buf, np.uint8).reshape(self.H, self.W, 3); self.pos += 1
        return self.last


_phone = None
def phone_frame(screen):
    """the recording inside a drawn phone (landscape): black body, rounded corners, a punch-hole camera"""
    from PIL import ImageDraw
    sw, sh = PhoneClip.W, PhoneClip.H
    x0, y0 = (VW - sw) // 2, 118
    f = backdrop(screen)
    im = Image.fromarray(f)
    d = ImageDraw.Draw(im)
    m = 30
    d.rounded_rectangle((x0 - m - 6, y0 - m - 6, x0 + sw + m + 6, y0 + sh + m + 6), 74, fill=(70, 74, 86))
    d.rounded_rectangle((x0 - m, y0 - m, x0 + sw + m, y0 + sh + m), 68, fill=(8, 8, 10))
    a = np.asarray(im).copy()
    # the screen, with rounded corners
    mask = Image.new('L', (sw, sh), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, sw - 1, sh - 1), 40, fill=255)
    mk = np.asarray(mask)[..., None] > 0
    region = a[y0:y0 + sh, x0:x0 + sw]
    a[y0:y0 + sh, x0:x0 + sw] = np.where(mk, screen, region)
    im = Image.fromarray(a)
    d = ImageDraw.Draw(im)
    cx, cy = x0 - m // 2 - 2, y0 + sh // 2
    d.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=(28, 30, 38))
    return np.asarray(im).copy()


def scene_phone(segs, head, sub):
    """segs: [(first frame of android_phone_60.mkv, frames)]"""
    global _phone
    n = sum(m for _, m in segs)
    def render(i):
        global _phone
        if _phone is None: _phone = PhoneClip(os.path.join(CAP, 'android_phone_60.mkv'))
        j = i
        for a, m in segs:
            if j < m: break
            j -= m
        f = phone_frame(_phone[a + j])
        f = label(f, 'ANDROID', x=VW // 2, y=36, size=40, anchor='center', alpha=ease(i / 12) * ease((n - 1 - i) / 12))
        return caption(f, head, sub, i, n, None, kicker=False)
    return n, render


def scene_montage(clips, per, head, sub):
    n = per * len(clips)
    def render(i):
        name, a, ext, game = clips[i // per]
        f = canvas(clip(name)[a + i % per], ext)
        return put_caption(f, head, sub, i, n - 1 - i, None, 1000)
    return n, render


def scene_end(n, name='v11_rainbow', a=600):
    def render(i):
        f = dim(backdrop(clip(name)[a + (i % 400)]), 60)
        f = centered_text(f, [
            ('MICKEY & DONALD COLLECTION', F_BOLD, 90, WHITE, 170),
            ('V1.1  •  WINDOWS & ANDROID  •  OUT NOW', F_BOLD, 52, ORANGE, 300),
            ('github.com/Darhkfox', F_BOLD, 50, WHITE, 410),
            ('Requires your own ROMs of the USA releases — not included', F_REG, 38, SUBC, 495),
            ('Castle of Illusion (USA)   •   QuackShot (USA / World, REV 00)   •   World of Illusion (USA)',
             F_REG, 30, SUBC, 550),
            ('MD+ soundtrack packs are optional and not included', F_REG, 34, SUBC, 605),
            ('youtube.com/adhgaming   •   u/Darhkwing', F_REG, 38, SUBC, 700),
            ('Fan project. Not affiliated with Disney or SEGA.', F_REG, 28, (150, 160, 180), 900),
        ], ease(i / S(0.8)))
        if i > n - S(1.2): f = fade(f, (n - i) / S(1.2))
        return f
    return n, render


# ---------------------------------------------------------------- timeline
def timeline():
    return [
        ('title',       scene_title(S(5))),
        # ---- achievements
        ('ach_welcome', ui('v11_welcome', 20, S(6.5), 'ACHIEVEMENTS',
                           '53 to earn across Castle of Illusion, QuackShot and World of Illusion\n'
                           'Beat the bosses, pull off tricky moves, find the secret ones')),
        ('ach_popup',   scene_popup('v11_hall1', 20, S(5.5), 'EARNED AS YOU PLAY',
                                    'They pop up the moment you get them\nSave states still work - it all counts from a fresh start')),
        ('ach_locked',  ui('v11_locked', 20, S(8.5), 'KEEPING IT FAIR',
                           'With achievements on, Level Select, Cheats, Fast Forward and Rewind are switched off\n'
                           'Turn achievements off any time to bring them back', game='coi')),
        ('ach_page',    ui_segments('v11_achpage', [(150, 190), (480, 110), (690, 160)], 'TRACK YOUR PROGRESS',
                           'Every game\'s list from the launcher\nSee what you\'ve earned - and what\'s still to find')),
        # ---- Castle of Illusion: widescreen all the way through
        ('wide_card',   scene_card(S(3.2), [('CASTLE OF ILLUSION', F_BOLD, 54, (196, 150, 255), 380),
                                            ('NOW WIDESCREEN ALL THE WAY THROUGH', F_BOLD, 74, WHITE, 460)],
                                   'v11_toy', 40)),
        ('tree_ba',     scene_ba('v11_tree_v10', 'v11_tree', 60, S(8), S(3.2), 'BOSS FIGHTS IN WIDESCREEN',
                                 'Version 1.0 kept the boss arenas boxed in\nNow the whole room fills the screen',
                                 'V1.0', 'V1.1')),
        ('bosses',      scene_seq([('v11_toy', 150, S(3.4), 108), ('v11_storm', 60, S(3.0), 108),
                                   ('v11_worm', 60, S(3.0), 108), ('v11_mizrabel', 80, S(3.4), 108)],
                                  'THE SAME FIGHTS, WIDER',
                                  'Each fight plays exactly as the original 4:3 arena\n'
                                  'The rest of the room is shown dimmed - and the bosses\' attacks still reach into it')),
        ('hall_ba',     scene_ba('v11_hall1_v10', 'v11_hall1', 330, S(7), S(2.8), 'THE CASTLE HALLWAY',
                                 'Mickey\'s walk to each round\'s door - now full width', 'V1.0', 'V1.1')),
        ('stairs',      plain('v11_hall4', 560, S(4.5), None, None, ext=108, game='coi')),
        ('rainbow',     plain('v11_rainbow', 760, S(6), 'ALL THE WAY TO THE TOWER',
                              'Even the last walk to Mizrabel\'s tower fills a 21:9 screen', ext=108, game='coi')),
        # ---- MD+ soundtracks
        ('md_audio',    ui('v11_audio', 40, S(6.5), 'CD-QUALITY SOUNDTRACKS',
                           'Castle of Illusion and QuackShot can play MD+ soundtrack packs\n'
                           'Drop a pack in the soundtracks folder and pick it under Audio')),
        ('md_coi',      plain('v11_md_forest', 0, S(7), 'CASTLE OF ILLUSION, ARRANGED',
                              'The music streams from the pack - the sound effects stay the console\'s own',
                              chip='MD+ SOUNDTRACK', game='coi', kicker=False)),
        ('md_qs',       plain('v11_md_duckburg', 0, S(6.5), 'QUACKSHOT, TOO',
                              'A new music option for Donald\'s adventure\nRewind and save states keep the soundtrack in step',
                              chip='MD+ SOUNDTRACK', game='qs', kicker=False)),
        ('md_mp',       ui_segments('v11_mp', MD_MP_SEGS, 'MUSIC PLAYER',
                           'Switch between the original and the arranged version with Tab or (Y)\n'
                           'Plus the pack\'s extra tracks - like every boss\'s own theme')),
        # ---- cheats and controls
        ('cheats_ui',   ui('v11_cheats_ui', 40, S(5.5), 'NEW CHEATS',
                           'Walk Faster, Moon Jump and Jump in Midair\nThe classic Game Genie codes, one switch away', game='coi')),
        ('cheats_play', plain('v11_cheats', 0, S(6.5), None, None, chips=[(0, S(6.5), 'WALK FASTER  •  MOON JUMP')],
                              game='coi')),
        ('hotkeys',     ui('v11_hotkeys', 200, S(9.3), 'REBIND EVERYTHING',
                           'Fast forward, rewind, menu, quick save, quick load and screenshot\nOn the keyboard and on your gamepad')),
        # ---- the rest
        ('plus',        scene_features([('v11_mizrabel', 200, 108), ('v11_rainbow', 300, 108)], S(3.2),
                                       ['Black frame insertion for 120 Hz screens',
                                        'Level select keeps your soundtrack playing',
                                        'Rewind and save states keep the music in step',
                                        'Sprite flicker, colour and filter options as before'])),
        ('android',     scene_phone([(60, 180), (840, 288), (1290, 282)], 'ON ANDROID TOO',
                                    'Touch controls, achievements, MD+ soundtracks and every widescreen fix\n'
                                    'The same update for phones and tablets - or plug in a controller')),
        ('others',      scene_montage([('qs_palace', 150, 40, 'qs'), ('woi_garden', 150, 40, 'woi'),
                                       ('qs_hideout', 150, 108, 'qs'), ('woi_sunny', 150, 64, 'woi')], S(2.2),
                                      'QUACKSHOT & WORLD OF ILLUSION',
                                      'Achievements and every new option in all three games')),
        ('end',         scene_end(S(9))),
    ]


# sound: (first scene of the section, recording, start offset in seconds); 'own' = the scenes' own recordings
MUSIC = [('title', 'mus_coi_title', 0.0), ('wide_card', 'mus_coi_boss', 0.0), ('hall_ba', 'mus_coi_castle', 0.0),
         ('md_audio', 'mus_md_coi_title', 0.0), ('md_coi', 'own', 0.0), ('cheats_ui', 'mus_coi_toyland', 0.0),
         ('android', 'mus_woi_title', 0.0), ('others', 'mus_qs_duckburg', 0.0), ('end', 'mus_woi_sky', 0.0)]
OWN = {'md_coi': ('v11_md_forest', [(0, 420)]), 'md_qs': ('v11_md_duckburg', [(0, 390)]), 'md_mp': ('v11_mp', MD_MP_SEGS)}
XFADE = 1.0
TARGET_RMS = 2300.0


def load_pcm(name, start, trim=True):
    a = np.fromfile(os.path.join(CAP, name + '.pcm'), np.int16).reshape(-1, 2).astype(np.float32)
    if trim:
        loud = np.nonzero(np.abs(a).max(1) > 300)[0]
        a = a[max(0, (loud[0] if len(loud) else 0) - 200):]
    return a[int(start * 48000):]


def make_bed(scenes):
    starts, t = {}, 0
    order = []
    for name, (n, _) in scenes:
        starts[name] = t; order.append(name); t += n
    total = int(t / FPS * 48000)
    bed = np.zeros((total, 2), np.float32)
    xf = int(XFADE * 48000)
    sec = lambda f: int(f / FPS * 48000)
    for k, (scene, rec, off) in enumerate(MUSIC):
        s0 = sec(starts[scene])
        s1 = sec(starts[MUSIC[k + 1][0]]) if k + 1 < len(MUSIC) else total
        if rec == 'own':
            # each scene's own recording, in step with its frames (frame a of the clip at the scene's first frame)
            seg = np.zeros((s1 - s0 + xf, 2), np.float32)
            for name in order[order.index(scene):]:
                if sec(starts[name]) >= s1: break
                r, segs = OWN[name]
                pcm = load_pcm(r, 0, trim=False)
                p0 = sec(starts[name]) - s0
                ramp = int(0.02 * 48000)
                pieces = []
                for a, m in segs:          # each cut gets a 20 ms fade either side (no clicks)
                    o = int(a / T.CAP_FPS * 48000)
                    piece = pcm[o:o + sec(m)].copy()
                    if len(piece) > 2 * ramp:
                        piece[:ramp] *= np.linspace(0, 1, ramp)[:, None]
                        piece[-ramp:] *= np.linspace(1, 0, ramp)[:, None]
                    pieces.append(piece)
                whole = np.concatenate(pieces)
                r = float(np.sqrt((whole ** 2).mean()))
                if r > 1: whole *= min(3.0, max(0.4, TARGET_RMS / r))     # (each scene on its own level)
                seg[p0:p0 + len(whole)] += whole[:len(seg) - p0]
        else:
            seg = load_pcm(rec, off)
        a0, a1 = max(0, s0 - xf // 2), min(total, s1 + xf // 2)
        if rec == 'own': piece = np.concatenate([np.zeros((s0 - a0, 2), np.float32), seg])[:a1 - a0]
        else: piece = seg[:a1 - a0]
        if len(piece) < a1 - a0: piece = np.concatenate([piece, np.zeros((a1 - a0 - len(piece), 2), np.float32)])
        # every section at about the same loudness (the tracks and recordings differ a lot)
        body = piece[s0 - a0:s1 - a0]
        r = float(np.sqrt((body ** 2).mean())) if len(body) else 0.0
        if r > 1: piece = piece * min(3.0, max(0.4, TARGET_RMS / r))
        env = np.ones(len(piece), np.float32)
        if k > 0: env[:xf] = np.linspace(0, 1, xf)
        if k + 1 < len(MUSIC): env[-xf:] = np.linspace(1, 0, xf)
        bed[a0:a1] += piece * env[:, None]
    fi, fo = int(0.3 * 48000), int(3.0 * 48000)
    bed[:fi] *= np.linspace(0, 1, fi)[:, None]
    bed[-fo:] *= np.linspace(1, 0, fo)[:, None]
    out = os.path.join(WORK, 'music_bed_v11.pcm')
    np.clip(bed * 1.1, -32768, 32767).astype(np.int16).tofile(out)
    return out


def build():
    scenes = timeline()
    total = sum(n for _, (n, _) in scenes)
    print('total %d frames = %.1f s' % (total, total / FPS), flush=True)
    music = make_bed(scenes)
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
        print(f'scene {k:2d} {name:12s}: {n:5d} frames  (t={done / FPS:6.1f}s)', flush=True)
    ff.stdin.close(); ff.wait()
    print('wrote', OUT_MP4, f'{done / FPS:.1f}s')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] in ('preview', 'stills'):
        out = os.path.join(WORK, 'preview_v11'); os.makedirs(out, exist_ok=True)
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
