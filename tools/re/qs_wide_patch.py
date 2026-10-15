r"""QuackShot (World, REV 00) widescreen ROM patches.  See docs/re_qs_wide.md.

    import sys; sys.path.insert(0, r'K:\projects\castle of illusion\tools\re')
    from qs_wide_patch import patches, apply
    apply(md, E=40)          # md = mdt.MD(QS); patches the loaded ROM copy (word writes)

Each patch is (name, rom_address, original_bytes, new_bytes).  `patches(E)` returns the
list for a given extension E (extra pixels on each side, 0 < E <= 80).
"""
import struct

# Offset (px) by which the 512-px nametable "valid window" is moved left of camera X.
# The original game keeps map columns [X, X+512) in the 64-cell planes; we keep
# [max(X-OFF,0), max(X-OFF,0)+512) (the clamp keeps line-scrolled background bands, which
# show the whole 512-px plane row, free of negative-X garbage near the level start).  OFF=96 centres the window on the screen centre (X+160) and
# satisfies E <= OFF <= 177-E for every E <= 80 (see doc section 2).
STREAM_OFF = 96


def _w(*words):
    return b''.join(struct.pack('>H', w & 0xFFFF) for w in words)


def stream_patches(off=STREAM_OFF):
    return [
        # --- column streaming ($26B4 part 1): draw column at X-off (moving left) or
        #     oldX+512-off (moving right) instead of X / oldX+512
        ('stream_col', 0x26C8,
         _w(0x6D0E, 0x3029, 0x0024, 0x3229, 0x0020, 0x4EBA, 0x00B2, 0x6010,
            0x3029, 0x002C, 0x0640, 0x0200, 0x3229, 0x0020, 0x4EBA, 0x00A0),
         _w(0x6D06, 0x3029, 0x0024, 0x6008,          # blt.s R / move.w $24(a1),d0 / bra.s C
            0x3029, 0x002C, 0x0640, 0x0200,          # R: move.w $2c(a1),d0 / addi.w #$200,d0
            0x0440, off, 0x6A02, 0x7000,             # C: subi.w #off,d0 / bpl.s +2 / moveq #0,d0
            0x3229, 0x0020, 0x4EBA, 0x00A0)),        # move.w $20(a1),d1 / jsr $2786(pc)
        # --- row streaming ($26B4 part 2): rows start at X-off instead of X
        ('stream_row', 0x26FC,
         _w(0x6D0C, 0x3029, 0x0024, 0x3229, 0x0020, 0x6000, 0x0014,
            0x3029, 0x0024, 0x3229, 0x0028, 0x0641, 0x0100, 0x6000, 0x0004),
         _w(0x6D06, 0x3229, 0x0020, 0x6008,          # blt.s D / move.w $20(a1),d1 / bra.s C
            0x3229, 0x0028, 0x0641, 0x0100,          # D: move.w $28(a1),d1 / addi.w #$100,d1
            0x3029, 0x0024, 0x0440, off,             # C ($270C): move.w $24(a1),d0 / subi.w #off,d0
            0x6A02, 0x7000, 0x6002)),                # bpl.s +2 / moveq #0,d0 / bra.s $271C
        # --- initial full-plane fill ($3FBC): call the new X-off row entry at $270C
        ('stream_init', 0x3FCE, _w(0x4EBA, 0xE74C), _w(0x4EBA, 0xE73C)),
    ]


def sprite_patches(E):
    lo = 0x60 - E            # piece X (VDP coords) below this is fully left of the view
    hi = 0x1C0 + E           # piece X at/after this is right of the view
    assert lo > 0 and hi <= 0x200, 'E out of range for the 9-bit sprite X'
    return [
        ('spr_xmin', 0x9BCE, _w(0x0C40, 0x0060), _w(0x0C40, lo)),
        ('spr_xmax', 0x9BD4, _w(0x0C40, 0x01C0), _w(0x0C40, hi)),
    ]


def object_patches():
    return [
        # left spawn: cell containing X-$F1 instead of X-$71 (one 128-px cell further left)
        ('spawn_left', 0x19C1C, _w(0x0440, 0x0071), _w(0x0440, 0x00F1)),
        # area start: 6 spawn columns (X-$F1 .. X+$18F) instead of 5
        ('spawn_init', 0x19D74, _w(0x0C47, 0x0280), _w(0x0C47, 0x0300)),
        # despawn/cull window: world X in [camX-256, camX+512) instead of [camX-128, camX+512)
        ('despawn', 0x9A9A,
         _w(0x0C44, 0x0280, 0x6400, 0x1C22, 0x4A44, 0x6B00, 0x1C1C),
         _w(0x3004, 0x0640, 0x0080, 0x0C40, 0x0300, 0x6400, 0x1C1C)),
    ]


FREE = 0x7B000        # inside the $FF-filled gap $7A79C..$7BF00 (unused padding)


def camera_clamp_patches(E):
    """OPTIONAL: keep plane A's camera E px inside its X bounds whenever the X range
    (Xmax-Xmin) is at least 2E+2 px, so the 320+2E view never shows outside the level.
    Narrower areas keep the original bounds (the renderer masks them, doc section 4)."""
    e = E << 16
    need = (2 * E + 2) << 16
    hook = _w(
        # $7B000: bounds test for $2656 (a1 = layer, d0 = velocity already added to X)
        0x2229, 0x0004,             # move.l  $4(a1),d1      lo
        0x2411,                     # move.l  (a1),d2        hi
        0xB2FC, 0x8500,             # cmpa.w  #$8500,a1      plane A only
        0x6618,                     # bne.s   chk
        0x2602,                     # move.l  d2,d3
        0x9681,                     # sub.l   d1,d3          range
        0x0C83, need >> 16, need & 0xFFFF,   # cmpi.l #need,d3
        0x6D0C,                     # blt.s   chk
        0x0681, e >> 16, e & 0xFFFF,         # addi.l #E,d1
        0x0482, e >> 16, e & 0xFFFF,         # subi.l #E,d2
        # chk ($7B024)
        0xB2A9, 0x0024,             # cmp.l   $24(a1),d1     lo >= X ?
        0x6C0C,                     # bge.s   low
        0xB4A9, 0x0024,             # cmp.l   $24(a1),d2     hi > X ?
        0x6E12,                     # bgt.s   ok
        0x4A80,                     # tst.l   d0             X >= hi: allow moving left
        0x6B0E,                     # bmi.s   ok
        0x6004,                     # bra.s   rev
        # low ($7B036)
        0x4A80,                     # tst.l   d0             X <= lo: allow moving right
        0x6E08,                     # bgt.s   ok
        # rev ($7B03A)
        0x91A9, 0x0024,             # sub.l   d0,$24(a1)
        0x42A9, 0x0034,             # clr.l   $34(a1)
        # ok ($7B042)
        0x4E75,                     # rts
    )
    assert len(hook) == 0x44, hex(len(hook))
    load = _w(
        # $7B044: area load, replaces "jsr $5000(pc); jsr $3F96(pc)" at $35A0
        0x4EB9, 0x0000, 0x5000,     # jsr     $5000
        0x43F8, 0x8500,             # lea     $8500.w,a1
        0x2229, 0x0004,             # move.l  $4(a1),d1
        0x2411,                     # move.l  (a1),d2
        0x2602,                     # move.l  d2,d3
        0x9681,                     # sub.l   d1,d3
        0x0C83, need >> 16, need & 0xFFFF,   # cmpi.l #need,d3
        0x6D00, 0x0068,             # blt.w   done
        0x0681, e >> 16, e & 0xFFFF,         # addi.l #E,d1   lo_E
        0x0482, e >> 16, e & 0xFFFF,         # subi.l #E,d2   hi_E
        0x2029, 0x0024,             # move.l  $24(a1),d0     X
        0xB081,                     # cmp.l   d1,d0
        0x6E0A,                     # bgt.s   c2
        0x2001,                     # move.l  d1,d0
        0x0680, 0x0001, 0x0000,     # addi.l  #$10000,d0     lo_E + 1 px
        0x600E,                     # bra.s   set
        # c2
        0xB082,                     # cmp.l   d2,d0
        0x6D00, 0x0044,             # blt.w   done
        0x2002,                     # move.l  d2,d0
        0x0480, 0x0001, 0x0000,     # subi.l  #$10000,d0     hi_E - 1 px
        # set
        0x2200,                     # move.l  d0,d1
        0x92A9, 0x0024,             # sub.l   $24(a1),d1     delta
        0x2340, 0x0024,             # move.l  d0,$24(a1)
        0x2340, 0x002C,             # move.l  d0,$2c(a1)
        0x21C1, 0x8534,             # move.l  d1,$8534.w     plane B follows by parallax ratio
        0x42B8, 0x8530,             # clr.l   $8530.w
        0x43F8, 0x8540,             # lea     $8540.w,a1
        0x4EB9, 0x0000, 0x25E0,     # jsr     $25E0
        0x2038, 0x8574,             # move.l  $8574.w,d0
        0xD1B8, 0x8564,             # add.l   d0,$8564.w
        0x21F8, 0x8564, 0x856C,     # move.l  $8564.w,$856c.w
        0x42B8, 0x8574,             # clr.l   $8574.w
        0x42B8, 0x8570,             # clr.l   $8570.w
        0x42B8, 0x8534,             # clr.l   $8534.w
        # done
        0x4EF9, 0x0000, 0x3F96,     # jmp     $3F96
    )
    assert len(load) == 0x8A, hex(len(load))
    ff = lambda n: bytes([0xFF]) * n
    return [
        ('cam_hook_code', FREE, ff(len(hook) + len(load)), hook + load),
        ('cam_bounds', 0x2664, _w(0x2229, 0x0004, 0xB2A9), _w(0x4EB9, 0x0007, 0xB000)),
        ('cam_bounds2', 0x266A, _w(0x0024), _w(0x6018)),   # bra.s $2684
        ('cam_load', 0x35A0, _w(0x4EBA, 0x1A5E, 0x4EBA, 0x09F0), _w(0x4EB9, 0x0007, 0xB044, 0x4E71)),
    ]


def checksum_patch():
    # boot checksum test at $402: bne.w $502 -> nop nop
    return [('checksum', 0x40A, _w(0x6600, 0x00F6), _w(0x4E71, 0x4E71))]


def patches(E=40, camera_clamp=False):
    p = []
    p += checksum_patch()
    p += stream_patches()
    p += sprite_patches(E)
    p += object_patches()
    if camera_clamp:
        p += camera_clamp_patches(E)
    return p


def apply(md, E=40, only=None, verify=True, camera_clamp=False):
    rom = md.rom
    for name, addr, orig, new in patches(E, camera_clamp):
        if only and name not in only:
            continue
        cur = bytes(rom[addr:addr + len(orig)])
        if verify and cur != orig and cur != new:
            raise ValueError('%s @%06X: ROM has %s, expected %s' % (name, addr, cur.hex(), orig.hex()))
        assert len(orig) == len(new), name
        for i in range(0, len(new), 2):
            md.patch16(addr + i, struct.unpack('>H', new[i:i + 2])[0])


def apply_bytes(rom_bytes, E=40, camera_clamp=False):
    """Return a patched copy of a ROM image (bytes)."""
    b = bytearray(rom_bytes)
    for name, addr, orig, new in patches(E, camera_clamp):
        assert bytes(b[addr:addr + len(orig)]) == orig, name
        b[addr:addr + len(new)] = new
    return bytes(b)
