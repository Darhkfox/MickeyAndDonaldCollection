#!/bin/sh
# Starts the Linux build hidden (virtual X display, software OpenGL ES, no sound device, no ROMs) and runs a short
# autotest script: the launcher, Options and Credits, a screenshot of each. Fails if the program fails or a picture is
# missing or blank.   linux/smoke_test.sh <built DisneyClassics> <out dir>
set -e
EXE="$1"; OUT="$2"
mkdir -p "$OUT"
cat > "$OUT/script.txt" <<'EOF'
90 shot launcher
100 tap down
120 tap left
140 tap left
160 tap enter
240 shot options
250 tap esc
290 tap right
310 tap right
330 tap right
350 tap enter
430 shot credits
440 quit
EOF
export LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy
timeout 600 xvfb-run -a -s "-screen 0 1280x800x24" "$EXE" --autotest "$OUT" "$OUT/script.txt" --capture-size 1280x800 \
    > "$OUT/log.txt" 2>&1 || { cat "$OUT/log.txt"; exit 1; }
cat "$OUT/log.txt"
for s in launcher options credits; do
    [ -s "$OUT/$s.png" ] || { echo "missing $s.png"; exit 1; }
done
python3 - "$OUT" <<'EOF'
import sys, zlib, struct
# (a minimal PNG reader: the pictures' average colour and how many distinct colours, to tell a drawn screen from a blank one)
def stats(path):
    d = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        if t == b'IHDR': w, h, depth, ctype = struct.unpack('>IIBB', d[pos + 8:pos + 18])
        if t == b'IDAT': idat += d[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    bpp = 4 if ctype == 6 else 3
    stride = w * bpp
    prev = bytearray(stride); colours = set(); total = 0; count = 0
    for y in range(h):
        f = raw[y * (stride + 1)]; line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if y % 8 == 0:
            for x in range(0, w, 8):
                px = tuple(line[x * bpp:x * bpp + 3]); colours.add(px); total += sum(px); count += 3
    return w, h, total / count, len(colours)
bad = False
for s in ('launcher', 'options', 'credits'):
    w, h, mean, n = stats('%s/%s.png' % (sys.argv[1], s))
    print('%-9s %dx%d  mean %.1f  colours %d' % (s, w, h, mean, n))
    bad = bad or n < 20
sys.exit(1 if bad else 0)
EOF
