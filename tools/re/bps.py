# BPS patch reader / applier (beat format: "BPS1", varints, SourceRead / TargetRead / SourceCopy / TargetCopy, three
# CRC32s at the end), and a report of the regions a patch changes.
#   python bps.py <rom> <patch.bps> [out.bin]
import sys, zlib


def apply(src, patch):
    if patch[:4] != b'BPS1':
        raise ValueError('not a BPS patch')
    pos = 4

    def num():
        nonlocal pos
        data, shift = 0, 1
        while True:
            x = patch[pos]; pos += 1
            data += (x & 0x7F) * shift
            if x & 0x80: return data
            shift <<= 7
            data += shift
    src_size, dst_size, meta = num(), num(), num()
    pos += meta
    crc_src, crc_dst, crc_patch = (int.from_bytes(patch[-12 + i * 4:-8 + i * 4], 'little') for i in range(3))
    if zlib.crc32(src) & 0xFFFFFFFF != crc_src:
        raise ValueError('source CRC %08X, patch wants %08X' % (zlib.crc32(src) & 0xFFFFFFFF, crc_src))
    out = bytearray(dst_size)
    o = src_rel = dst_rel = 0
    end = len(patch) - 12
    while pos < end:
        d = num()
        cmd, length = d & 3, (d >> 2) + 1
        if cmd == 0:                       # SourceRead
            out[o:o + length] = src[o:o + length]; o += length
        elif cmd == 1:                     # TargetRead
            out[o:o + length] = patch[pos:pos + length]; pos += length; o += length
        else:
            d = num()
            off = (-1 if d & 1 else 1) * (d >> 1)
            if cmd == 2:                   # SourceCopy
                src_rel += off
                out[o:o + length] = src[src_rel:src_rel + length]; src_rel += length; o += length
            else:                          # TargetCopy (may overlap itself)
                dst_rel += off
                for _ in range(length):
                    out[o] = out[dst_rel]; o += 1; dst_rel += 1
    if zlib.crc32(bytes(out)) & 0xFFFFFFFF != crc_dst:
        raise ValueError('target CRC mismatch')
    return bytes(out)


def regions(a, b, gap=16):
    """[(start, end)] where b differs from a (or extends it), merging changes closer than `gap` bytes"""
    n = max(len(a), len(b))
    out, start, last = [], None, None
    for i in range(n):
        if (a[i] if i < len(a) else None) != b[i] if i < len(b) else True:
            if start is None: start = i
            elif i - last > gap: out.append((start, last + 1)); start = i
            last = i
    if start is not None: out.append((start, last + 1))
    return out


if __name__ == '__main__':
    src = open(sys.argv[1], 'rb').read()
    out = apply(src, open(sys.argv[2], 'rb').read())
    print('source %d bytes, target %d bytes' % (len(src), len(out)))
    for s, e in regions(src, out):
        print('  $%06X-$%06X  (%d bytes)%s' % (s, e - 1, e - s, '  beyond the original ROM' if s >= len(src) else ''))
    if len(sys.argv) > 3: open(sys.argv[3], 'wb').write(out)
