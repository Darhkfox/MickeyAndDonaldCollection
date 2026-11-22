"""Mickey & Donald Collection v1.3 update trailer: the look of the earlier trailers (trailer.py's helpers), footage from
v13_rec.py. Focus: the Theatre menus and your own covers, Castle of Illusion's enemies in step with the original in
widescreen (GitHub issue #19), QuackShot's REV 01 support and widescreen fixes (simplePsyduck), achievement export.

  python trailer_v13.py                          full encode -> trailer_work/Mickey_and_Donald_Collection_v1.3_update.mp4
  python trailer_v13.py preview [scene ...]      middle still of each scene
  python trailer_v13.py stills scene n           n evenly spaced stills of a scene
"""
import os, sys, subprocess
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import trailer as T  # noqa: E402
from trailer import (S, FPS, CAP, WORK, VW, VH, F_BOLD, F_REG, ORANGE, WHITE, SUBC, clip, canvas, ui_canvas,  # noqa: E402
                     caption, label, centered_text, fade, dim, ease, backdrop, put_caption, plain, ui, scene_features)
from trailer_v11 import scene_card, ui_segments, scene_montage  # noqa: E402

OUT_MP4 = os.path.join(WORK, 'Mickey_and_Donald_Collection_v1.3_update.mp4')
V13 = os.path.join(WORK, 'v13')


# ---------------------------------------------------------------- scenes
def scene_title(n, name='v13_launcher', a=200):
    def render(i):
        f = dim(clip(name)[a + i], 30)
        f = fade(f, ease(i / S(1.0)))
        ta = ease((i - S(0.5)) / S(0.8))
        f = centered_text(f, [('MICKEY & DONALD COLLECTION', F_BOLD, 96, WHITE, 330),
                              ('VERSION 1.3', F_BOLD, 78, ORANGE, 460),
                              ('A NEW LOOK AND BIG FIXES', F_BOLD, 52, WHITE, 575)], ta)
        if i > n - S(0.5): f = fade(f, (n - i) / S(0.5))
        return f
    return n, render


def ui_card(name, a, n, lines):
    """a text card over a dimmed app screen recording"""
    def render(i):
        f = dim(clip(name)[a + i], 35)
        return centered_text(f, lines, ease(i / S(0.4)) * ease((n - 1 - i) / S(0.4)))
    return n, render


# #19: v1.2 and v1.3 at 16:9 side by side; where a panel's picture differs from the original 4:3 game's (same moment,
# same camera), the original's pixels show through tinted cyan - its enemies' places
GW = 944                      # a panel: the 16:9 game picture (1920x1075 in the recording) at this width
GH = int(round(1075 * GW / 1920))
PY = 140


def ghosted(wide, orig43):
    """wide: a 16:9 recording frame; orig43: the 4:3 one. The 4:3 picture covers the wide one's middle 320 columns."""
    g = np.asarray(Image.fromarray(orig43[0:1080, 188:188 + 1543]).resize((1536, 1075), Image.BILINEAR)).astype(np.int16)
    w = wide[2:2 + 1075].astype(np.int16).copy()
    mid = w[:, 192:192 + 1536]
    diff = np.abs(mid - g).max(axis=2) > 60
    diff[int(1075 * 192 / 224):] = False          # (not the status bar: widescreen re-anchors it)
    tint = (g * 0.35 + np.array([40, 230, 255]) * 0.65)
    mid[diff] = (mid[diff] * 0.35 + tint[diff] * 0.65).astype(np.int16)
    w[:, 192:192 + 1536] = mid
    return np.clip(w, 0, 255).astype(np.uint8)


def scene_compare(a, n, head, sub):
    def render(i):
        o43 = clip('v13_storm_43')[a + i]
        f = backdrop(clip('v13_storm_w')[a + i])
        for k, (name, chip) in enumerate((('v13_storm_o', 'V1.2'), ('v13_storm_w', 'V1.3'))):
            p = ghosted(clip(name)[a + i], o43)
            p = np.asarray(Image.fromarray(p).resize((GW, GH), Image.LANCZOS))
            x = 16 + k * (GW + 16)
            f[PY:PY + GH, x:x + GW] = p
            f = label(f, chip, x=x + GW // 2, y=PY - 92, size=48, anchor='center')
        f = label(f, 'CYAN = WHERE THE ORIGINAL 4:3 GAME HAS THEM', x=VW // 2, y=PY + GH + 26, size=30, anchor='center')
        return caption(f, head, sub, i, n, 'coi', 40, kicker=True, ui=True)
    return n, render


def scene_badges(n, name='v13_export', a=400):
    wall = Image.open(os.path.join(V13, 'badge_wall.png'))
    def render(i):
        f = dim(backdrop(clip(name)[a]), 70)
        base = Image.fromarray(f).convert('RGBA')
        k = ease(i / S(0.8))
        w = int(wall.width * (0.9 + 0.1 * k)); h = int(wall.height * (0.9 + 0.1 * k))
        lay = wall.resize((w, h), Image.LANCZOS)
        if k < 1:
            al = np.asarray(lay).copy(); al[..., 3] = (al[..., 3] * k).astype(np.uint8); lay = Image.fromarray(al)
        base.alpha_composite(lay, ((VW - w) // 2, 90 + (wall.height - h) // 2))
        f = np.asarray(base.convert('RGB')).copy()
        return caption(f, 'A BADGE FOR EVERY ONE', 'All 53, written next to achievements.csv\nReady for Playnite or your own collection page',
                       i, n, None, kicker=False)
    return n, render


def scene_end(n, name='v11_rainbow', a=600):
    def render(i):
        f = dim(backdrop(clip(name)[a + (i % 400)]), 60)
        f = centered_text(f, [
            ('MICKEY & DONALD COLLECTION', F_BOLD, 90, WHITE, 150),
            ('V1.3  •  WINDOWS & ANDROID  •  OUT NOW', F_BOLD, 52, ORANGE, 280),
            ('github.com/Darhkfox', F_BOLD, 50, WHITE, 385),
            ('Requires your own ROMs of the USA releases — not included', F_REG, 38, SUBC, 470),
            ('Castle of Illusion (USA)   •   QuackShot (USA / World, REV 00 or REV 01)   •   World of Illusion (USA)',
             F_REG, 30, SUBC, 525),
            ('Thanks to simplePsyduck (QuackShot REV 01 and widescreen fixes) and GreenSnakeLinux (Android controllers)',
             F_REG, 30, SUBC, 600),
            ('youtube.com/adhgaming   •   u/Darhkwing   •   patreon.com/TeamRedTail', F_REG, 38, SUBC, 700),
            ('Fan project. Not affiliated with Disney or SEGA.', F_REG, 28, (150, 160, 180), 900),
        ], ease(i / S(0.8)))
        if i > n - S(1.2): f = fade(f, (n - i) / S(1.2))
        return f
    return n, render


# the QuackShot recording (21:9 from power-on): the cast scroll, the title, the START / OPTIONS menu, the map's airplane
QS_INTRO_A = [(700, 250), (2230, 330)]
QS_INTRO_B = [(2675, 95), (2950, 235)]


def gameplay_segments(name, segs, head, sub, ext, game, chip=None):
    n = sum(m for _, m in segs)
    def render(i):
        j = i
        for a, m in segs:
            if j < m: break
            j -= m
        f = canvas(clip(name)[a + j], ext)
        if chip: f = label(f, chip, y=T.TOP_21 + 40 if ext == 108 else 110)
        return caption(f, head, sub, i, n, game, ext)
    return n, render


# ---------------------------------------------------------------- timeline
def timeline():
    return [
        ('title',      scene_title(S(5))),
        # ---- the new look
        ('theatre',    ui('v13_launcher', 300, S(7), 'A NEW LOOK: THEATRE',
                          'Velvet panels in gold frames, cartoon titles and a starry stage behind the games\n'
                          'The live previews of all three games, as before')),
        ('classic',    ui_segments('v13_style', [(285, 110), (455, 160), (825, 210)], 'OR KEEP IT CLASSIC',
                          'Options > Launcher & Menus > Menu Style\nSwitch any time - the menus follow at once')),
        ('covers',     ui_segments('v13_covers', [(0, 380), (980, 300)], 'YOUR OWN COVERS AND BACKGROUND',
                          'Put PNG or JPEG pictures in the covers folder (none are included - these are made from\n'
                          'each game\'s title screen). Shown whole or filling the frame')),
        ('menus',      ui_segments('v13_menus', [(20, 340)], 'THE SAME LOOK IN EVERY MENU',
                          'The pause menu and options in the Theatre style - or Classic', game='qs')),
        # ---- Castle of Illusion #19
        ('coi_card',   scene_card(S(3.4), [('CASTLE OF ILLUSION', F_BOLD, 54, (196, 150, 255), 360),
                                           ('ENEMIES IN STEP WITH THE ORIGINAL', F_BOLD, 74, WHITE, 440),
                                           ('IN WIDESCREEN', F_BOLD, 56, ORANGE, 540)],
                                  'v13_storm_w', 30, ext=40)),
        ('after',      plain('v13_storm_w', 40, S(4.2), 'EVERY ENCOUNTER AS IN 4:3',
                             'An enemy that comes into the wider view early stands still until the original creates it\n'
                             'Fixed for every level - thanks for the report (GitHub issue #19)', ext=40, game='coi',
                             chip='V1.3')),
        ('compare',    scene_compare(300, S(4.9), 'THE SAME MOVES, THE SAME ENEMIES',
                                     'v1.2: in widescreen enemies started early and were somewhere else when Mickey got there\n'
                                     'v1.3: they wait for the original\'s moment - here v1.2 takes a hit the original never takes')),
        # ---- QuackShot
        ('qs_card',    scene_card(S(3.4), [('QUACKSHOT', F_BOLD, 54, (255, 200, 70), 360),
                                           ('REV 01 SUPPORT AND WIDESCREEN FIXES', F_BOLD, 70, WHITE, 440),
                                           ('THANKS TO SIMPLEPSYDUCK', F_BOLD, 50, ORANGE, 540)],
                                  'qs_ultra', 120, ext=108)),
        ('qs_intro',   gameplay_segments('v13_qs_intro', QS_INTRO_A, 'THE INTRO AND TITLE, NOW WIDE',
                                         'The cast scroll, then Donald\'s plane flying in across a 21:9 screen', 108, 'qs')),
        ('qs_map',     gameplay_segments('v13_qs_intro', QS_INTRO_B, 'MENUS AND MAP TOO',
                                         'The airplane crosses the whole world map', 108, 'qs')),
        ('qs_fixes',   scene_features([('qs_egypt', 150, 40), ('qs_south', 150, 40), ('qs_duckburg', 200, 40)], S(2.6),
                                      ['Both releases: REV 00 and REV 01 (own save states)',
                                       'Egypt\'s guards no longer pop up mid-screen',
                                       'The Duckburg blimp flies in from off screen',
                                       'Worms, sparks, penguins and statues fixed',
                                       'MD+ soundtracks carried over to REV 01'], heading='QUACKSHOT WIDESCREEN')),
        ('qs_option',  ui_segments('v13_menus', [(380, 230)], 'OPTIONAL: ENEMIES BEHIND DONALD',
                          'Enemies the game makes just behind Donald start beyond the wide view\'s edge\n'
                          'and catch up with their 4:3 place (Options > Video)', game='qs')),
        # ---- achievements
        ('export',     ui_segments('v13_export', [(120, 470)], 'EXPORT YOUR ACHIEVEMENTS',
                          'Achievements > Export to CSV: Playnite\'s columns, with the date you earned each one')),
        ('badges',     scene_badges(S(4.5))),
        # ---- the rest
        ('plus',       scene_features([('v11_rainbow', 300, 108), ('woi_sunny', 150, 64)], S(3.4),
                                      ['Update notifications on the launcher',
                                       'CPU Overclock takes out the games\' own slowdown',
                                       'Show FPS: the game\'s frame rate too',
                                       'Nintendo-style menu buttons option',
                                       'Controller fixes on Windows and Android'])),
        ('others',     scene_montage([('coi_ultra', 150, 108, 'coi'), ('qs_hideout', 150, 108, 'qs'),
                                      ('woi_garden', 150, 40, 'woi'), ('qs_palace', 150, 40, 'qs')], S(2.2),
                                     'WINDOWS & ANDROID', 'The same update for PCs, phones and tablets')),
        ('end',        scene_end(S(9))),
    ]


# sound: (first scene of the section, recording, start offset in seconds); 'own' = the scenes' own recordings
MUSIC = [('title', 'mus_coi_title', 0.0), ('coi_card', 'mus_coi_boss', 0.0), ('qs_card', 'mus_qs_title', 0.0),
         ('qs_intro', 'own', 0.0), ('qs_fixes', 'mus_qs_duckburg', 0.0), ('export', 'mus_woi_title', 0.0),
         ('others', 'mus_coi_toyland', 0.0), ('end', 'mus_woi_sky', 0.0)]
OWN = {'qs_intro': ('v13_qs_intro', QS_INTRO_A), 'qs_map': ('v13_qs_intro', QS_INTRO_B)}
XFADE = 1.0
TARGET_RMS = 2300.0


def load_pcm(name, start, trim=True):
    a = np.fromfile(os.path.join(CAP, name + '.pcm'), np.int16).reshape(-1, 2).astype(np.float32)
    if trim:
        loud = np.nonzero(np.abs(a).max(1) > 300)[0]
        a = a[max(0, (loud[0] if len(loud) else 0) - 200):]
    return a[int(start * 48000):]


def make_bed(scenes):
    starts, t, order = {}, 0, []
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
            seg = np.zeros((s1 - s0 + xf, 2), np.float32)
            for name in order[order.index(scene):]:
                if sec(starts[name]) >= s1: break
                r, segs = OWN[name]
                pcm = load_pcm(r, 0, trim=False)
                p0 = sec(starts[name]) - s0
                ramp = int(0.02 * 48000)
                pieces = []
                for a, m in segs:
                    o = int(a / T.CAP_FPS * 48000)
                    piece = pcm[o:o + sec(m)].copy()
                    if len(piece) > 2 * ramp:
                        piece[:ramp] *= np.linspace(0, 1, ramp)[:, None]
                        piece[-ramp:] *= np.linspace(1, 0, ramp)[:, None]
                    pieces.append(piece)
                whole = np.concatenate(pieces)
                r = float(np.sqrt((whole ** 2).mean()))
                if r > 1: whole *= min(3.0, max(0.4, TARGET_RMS / r))
                seg[p0:p0 + len(whole)] += whole[:len(seg) - p0]
        else:
            seg = load_pcm(rec, off)
        a0, a1 = max(0, s0 - xf // 2), min(total, s1 + xf // 2)
        if rec == 'own': piece = np.concatenate([np.zeros((s0 - a0, 2), np.float32), seg])[:a1 - a0]
        else: piece = seg[:a1 - a0]
        if len(piece) < a1 - a0: piece = np.concatenate([piece, np.zeros((a1 - a0 - len(piece), 2), np.float32)])
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
    out = os.path.join(WORK, 'music_bed_v13.pcm')
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
        out = os.path.join(WORK, 'preview_v13'); os.makedirs(out, exist_ok=True)
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
