"""Demo pictures for the v1.3 trailer's "your own covers" scene (trailer_work/v13/covers): a portrait cover per game made
from the game's own title screen (no box art), and a launcher background from a 21:9 gameplay frame."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
ROOT = r'K:\projects\castle of illusion'
V13 = os.path.join(ROOT, 'trailer_work', 'v13')
OUT = os.path.join(V13, 'covers')
LG = os.path.join(ROOT, 'third_party', 'fonts', 'LuckiestGuy-Regular.ttf')
os.makedirs(OUT, exist_ok=True)

GAMES = {'coi': ('title_coi_68.png', 'title_coi_13.png', 'CASTLE OF ILLUSION', 'Starring Mickey Mouse', (120, 70, 200)),
         'qs': ('title_qs_47.png', 'title_qs_56.png', 'QUACKSHOT', 'Starring Donald Duck', (210, 110, 20)),
         'woi': ('title_woi_57.png', 'title_woi_25.png', 'WORLD OF ILLUSION', 'Mickey Mouse & Donald Duck', (30, 90, 180))}


def text_centered(d, y, text, f, fill, w=600, shadow=True):
    tw = d.textbbox((0, 0), text, font=f)[2]
    if shadow: d.text(((w - tw) // 2 + 3, y + 3), text, font=f, fill=(0, 0, 0, 180))
    d.text(((w - tw) // 2, y), text, font=f, fill=fill)


for gid, (title, scene, name, sub, col) in GAMES.items():
    t = Image.open(os.path.join(V13, title)).convert('RGB')
    s = Image.open(os.path.join(V13, scene)).convert('RGB')
    cv = Image.new('RGB', (600, 840), col)
    bg = s.resize((1200, 840), Image.BILINEAR).crop((300, 0, 900, 840)).filter(ImageFilter.GaussianBlur(10))
    cv.paste(Image.eval(bg, lambda v: v * 55 // 100), (0, 0))
    d = ImageDraw.Draw(cv, 'RGBA')
    d.rectangle((0, 0, 599, 110), fill=col + (235,))
    f1 = ImageFont.truetype(LG, 58 if len(name) < 12 else 46)
    text_centered(d, 22, name, f1, (255, 230, 120))
    big = t.resize((560, 392), Image.NEAREST)
    d.rounded_rectangle((14, 136, 586, 540), 12, fill=(255, 255, 255, 230))
    cv.paste(big, (20, 142))
    small = s.resize((400, 280), Image.NEAREST)
    d.rounded_rectangle((94, 566, 506, 852), 10, fill=(255, 255, 255, 200))
    cv.paste(small, (100, 572))
    d.rectangle((0, 770, 599, 839), fill=(10, 10, 20, 230))
    text_centered(d, 784, sub.upper(), ImageFont.truetype(LG, 34), (255, 255, 255))
    cv.save(os.path.join(OUT, gid + '.png'))

# background: a 21:9 Castle of Illusion frame (Dessert Land), the game picture filling 1920x1080
from_trailer = os.path.join(V13, 'bg_source.png')
if os.path.exists(from_trailer):
    im = Image.open(from_trailer).convert('RGB')
    w, h = im.size
    sc = max(1920 / w, 1080 / h)
    im = im.resize((int(w * sc + 1), int(h * sc + 1)), Image.LANCZOS)
    x0, y0 = (im.width - 1920) // 2, (im.height - 1080) // 2
    im.crop((x0, y0, x0 + 1920, y0 + 1080)).save(os.path.join(OUT, 'background.png'))
print('covers in', OUT)
