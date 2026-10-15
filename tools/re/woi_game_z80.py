"""Z80 driver image helpers for World of Illusion (uses the generic disassembler in coi_game_z80dis).

    python woi_game_z80.py 0x0000 60      # disassemble the WOI driver image

The 68000 routine $0F6000 uploads (Z80 bus held, Z80 reset):
    ROM $0F60E4..$0F6EB3 ($DD0 bytes) -> Z80 $0000..$0DCF   (driver code + tables)
    ROM $0F6EB4..$0F7B3B ($C88 bytes) -> Z80 $0E00..$1A87   (driver data)
    ROM $0F606E..$0F6077 (10 bytes)   -> Z80 $1C00..$1C09   (variables init)
    RAM $FFFFCE (region flag)         -> Z80 $1BFF
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coi_game_z80dis import dis1, disz80  # noqa: E402,F401

HERE = os.path.dirname(os.path.abspath(__file__))
ROMPATH = os.path.join(os.path.dirname(os.path.dirname(HERE)),
                       'World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin')


def z80_image():
    rom = open(ROMPATH, 'rb').read()
    img = bytearray(0x2000)
    img[0x0000:0x0DD0] = rom[0xF60E4:0xF60E4 + 0xDD0]
    img[0x0E00:0x0E00 + 0xC88] = rom[0xF6EB4:0xF6EB4 + 0xC88]
    img[0x1C00:0x1C0A] = rom[0xF606E:0xF606E + 10]
    return bytes(img)


if __name__ == '__main__':
    mem = z80_image()
    a = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    print(disz80(mem, a, n))
