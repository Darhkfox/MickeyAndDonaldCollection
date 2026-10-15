r"""World of Illusion (USA) widescreen ROM patches.  See docs/re_woi_wide.md.

    import sys; sys.path.insert(0, r'K:\projects\castle of illusion\tools\re')
    from woi_wide_patch import patches, apply
    apply(md, E=40)          # md = mdt.MD(WOI); patches the loaded ROM copy (word writes)

Each patch is (name, rom_address, original_bytes, new_bytes).  `patches(E)` returns the list for
an extension of E extra pixels on each side (1 <= E <= 63; E = 20 -> 360x224, E = 40 -> 400x224).
Plane streaming needs no patch for E <= 81 (the game already keeps [camX-96, camX+401) valid).
New code is placed in the unused $FF padding at the end of the ROM ($0FFC00..).
"""
import struct

FREE = 0x0FFC00          # $0FFAFA-$0FFFFF is $FF padding
T_SPAWN = FREE           # horizontal spawn trigger (phase-shifted by +-E)
T_ANIM = FREE + 0x40     # map-animation visibility helper


def _w(*words):
    return b''.join(struct.pack('>H', w & 0xFFFF) for w in words)


def _bra_w(frm, to):
    """displacement word for a Bcc.w/BRA.w whose opcode is at `frm`"""
    return (to - (frm + 2)) & 0xFFFF


def checksum_patch():
    # boot checksum ($4E0): cmp.w (a1),d1 / bne.b $500 (red screen) -> nop
    return [('checksum', 0x4FC, _w(0x6602), _w(0x4E71))]


def sprite_patches(E):
    assert 1 <= E <= 63, 'VDP sprite X is 9 bits: $1C0+E must stay <= $1FF'
    p = []
    # --- per-piece X clip in the sprite builder ($BBE4): keep VDP x in [$60-E, $1C0+E)
    #     (original: screen x in [-32, 320) -> pieces right of 320 were dropped)
    p.append(('spr_clip_left', 0xBBEC, _w(0x0C40, 0x0060), _w(0x0C40, 0x60 - E)))
    p.append(('spr_clip_right', 0xBBF2, _w(0x0C40, 0x01C0), _w(0x0C40, 0x1C0 + E)))
    # --- whole-object cull = despawn ($BAB6, in $BA34): original deletes the object (jump $F600)
    #     when objX-camX is outside [-128,512) or objY-camY outside [-128,512).  New: X window
    #     [-128-E, 512+E); Y unchanged.  (the tst/bmi pairs were redundant with the unsigned bcc)
    orig = _w(0x0C44, 0x0280, 0x6400, 0x3B44, 0x4A44, 0x6B00, 0x3B3E,
              0x0C43, 0x0280, 0x6400, 0x3B36, 0x4A43, 0x6B00, 0x3B30)
    new = _w(0x0644, E,                            # addi.w #E,d4
             0x0C44, 0x280 + 2 * E,                # cmpi.w #$280+2E,d4
             0x6400, _bra_w(0xBABE, 0xF600),       # bcc.w  $F600
             0x0444, E,                            # subi.w #E,d4
             0x0C43, 0x0280,                       # cmpi.w #$280,d3
             0x6400, _bra_w(0xBACA, 0xF600),       # bcc.w  $F600
             0x4E71, 0x4E71)
    p.append(('obj_cull', 0xBAB6, orig, new))
    return p


def spawn_patches(E):
    p = []
    # --- horizontal spawn trigger $209FA: original spawns the 128-px cell column at camX+$180
    #     (moving right) / camX-$71 (moving left) whenever camX crosses a 128-px boundary.
    #     New: same test on camX+E (moving right) / camX-E (moving left), i.e. the original
    #     behaviour relative to the widened view edges.
    orig = _w(0x302C, 0x0010, 0x362C, 0x0020, 0x3400)
    new = _w(0x4EF9, T_SPAWN >> 16, T_SPAWN & 0xFFFF, 0x4E71, 0x4E71)   # jmp T_SPAWN
    p.append(('spawn_h_hook', 0x209FA, orig, new))
    code = _w(0x302C, 0x0010,        # move.w $10(a4),d0   camX
              0x362C, 0x0020,        # move.w $20(a4),d3   prevX
              0x3400,                # move.w d0,d2
              0x9443,                # sub.w  d3,d2        delta
              0x6B0A,                # bmi.s  .left
              0x0640, E,             # addi.w #E,d0
              0x0643, E,             # addi.w #E,d3
              0x6008,                # bra.s  .chk
              0x0440, E,             # .left: subi.w #E,d0
              0x0443, E,             # subi.w #E,d3
              0xB143,                # .chk: eor.w d0,d3
              0x0803, 0x0007,        # btst #7,d3
              0x6706,                # beq.s .ret
              0x4EF9, 0x0002, 0x0A7E,  # jmp $20A7E (spawn column; d0 = camX+-E, d2 = delta)
              0x4E75)                # .ret: rts
    p.append(('spawn_h_code', T_SPAWN, b'\xff' * len(code), code))
    # --- vertical spawn trigger $20A12 spawns 5 cell columns floor(camX/128)-1..+3; new:
    #     6 columns starting at floor((camX-E)/128)-1 (d0 is not used by the spawner $20AD4)
    p.append(('spawn_v_cols', 0x20A4A, _w(0x302C, 0x0010, 0x0240, 0xFF80, 0x3400),
              _w(0x342C, 0x0010, 0x0442, E, 0x4E71)))        # move.w $10(a4),d2 / subi.w #E,d2
    p.append(('spawn_v_count', 0x20A60, _w(0x7E04), _w(0x7E05)))
    # --- level-start spawn $20BBC: 5 columns camX-$71+{0..$200} -> 6 columns (..+$280)
    p.append(('spawn_init', 0x20BD6, _w(0x0C47, 0x0280), _w(0x0C47, 0x0300)))
    return p


def anim_patches(E):
    """Map-animation areas ($ABB2/$ADE2) only animate while inside blocks [camX/16, camX/16+20]."""
    k = 20 + (2 * E + 15) // 16
    code = _w(0x3438, 0x8510,        # move.w $8510.w,d2
              0x0442, E,             # subi.w #E,d2
              0xE842,                # asr.w #4,d2
              0x6A02,                # bpl.s .ok
              0x7400,                # moveq #0,d2
              0x4E75)                # .ok: rts
    return [
        ('anim_hook', 0xADE6, _w(0x3438, 0x8510, 0xE842), _w(0x4EB9, T_ANIM >> 16, T_ANIM & 0xFFFF)),
        ('anim_code', T_ANIM, b'\xff' * len(code), code),
        ('anim_right', 0xADF6, _w(0x0642, 0x0014), _w(0x0642, k)),
    ]


def p2_patches(E):
    """2-player keep-both-players-on-screen rule ($7D46): players are clamped to
    [camX+16, camX+304] -> [camX+16-E, camX+304+E]."""
    return [
        ('p2_clamp_left', 0x7D4A, _w(0x0640, 0x0010), _w(0x0640, 0x10 - E)),
        ('p2_clamp_right', 0x7D52, _w(0x0640, 0x0120), _w(0x0640, 0x120 + 2 * E)),
    ]


def autoscroll_patches(E):
    """Auto-scroll level script $906C (W5 act 6): the player is kept inside [camX+16, camX+312]
    ($FCF2 = min, $FCF0 = max, written every frame at $90B4/$90BC and $90E2/$90EA; applied by $BF8A).
    New: [camX+16-E, camX+312+E]."""
    return [
        ('autoscroll_min1', 0x90B4, _w(0x0640, 0x0010), _w(0x0640, 0x10 - E)),
        ('autoscroll_max1', 0x90BC, _w(0x0640, 0x0128), _w(0x0640, 0x128 + 2 * E)),
        ('autoscroll_min2', 0x90E2, _w(0x0640, 0x0010), _w(0x0640, 0x10 - E)),
        ('autoscroll_max2', 0x90EA, _w(0x0640, 0x0128), _w(0x0640, 0x128 + 2 * E)),
    ]


def appear_patches(E):
    """Object type 65 (W4 act 8) stays hidden+frozen until objX <= camX+320 ($1B4B4), i.e. it popped
    in at the old right edge.  New: camX+320+E."""
    return [('obj65_appear', 0x1B4B8, _w(0x0640, 0x0140), _w(0x0640, 0x140 + E))]


GROUPS = ('checksum', 'sprite', 'spawn', 'anim', 'p2', 'autoscroll', 'appear')


def patches(E=40, groups=GROUPS):
    p = []
    if 'checksum' in groups:
        p += checksum_patch()
    if 'sprite' in groups:
        p += sprite_patches(E)
    if 'spawn' in groups:
        p += spawn_patches(E)
    if 'anim' in groups:
        p += anim_patches(E)
    if 'p2' in groups:
        p += p2_patches(E)
    if 'autoscroll' in groups:
        p += autoscroll_patches(E)
    if 'appear' in groups:
        p += appear_patches(E)
    return p


def apply(md, E=40, groups=GROUPS, verify=True):
    rom = md.rom
    for name, addr, orig, new in patches(E, groups):
        assert len(orig) == len(new), name
        cur = bytes(rom[addr:addr + len(orig)])
        if verify and cur != orig and cur != new:
            raise ValueError('%s @%06X: ROM has %s, expected %s' % (name, addr, cur.hex(), orig.hex()))
        for i in range(0, len(new), 2):
            md.patch16(addr + i, struct.unpack('>H', new[i:i + 2])[0])


def apply_bytes(rom_bytes, E=40, groups=GROUPS):
    """Return a patched copy of a ROM image (bytes)."""
    b = bytearray(rom_bytes)
    for name, addr, orig, new in patches(E, groups):
        assert bytes(b[addr:addr + len(orig)]) == orig, name
        b[addr:addr + len(new)] = new
    return bytes(b)


if __name__ == '__main__':
    import sys
    E = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    for name, addr, orig, new in patches(E):
        print('%-16s $%06X  %s -> %s' % (name, addr, orig.hex(), new.hex()))
