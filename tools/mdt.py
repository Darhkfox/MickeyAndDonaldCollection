"""Python wrapper around mdtool.dll (headless Mega Drive core) for analysis.

Usage:
    from mdt import MD, COI, QS
    md = MD(COI)            # or MD(QS), MD(WOI)
    md.run(300)             # run 300 frames
    md.press('START', 5)    # hold START for 5 frames then release
    md.png('out.png')       # screenshot (scale 2 by default)
    md.ram16(0xFF1234)      # read RAM
"""
import ctypes as C
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COI = os.path.join(ROOT, 'Mickey Mouse - Castle of Illusion (U) [!].bin')
QS = os.path.join(ROOT, 'Quack Shot Starring Donald Duck (JUE) (REV 00) [!].bin')
WOI = os.path.join(ROOT, 'World of Illusion Starring Mickey Mouse and Donald Duck (U) [!].bin')

# tools/bin holds the analysis build that all saved .state files were made with; the CMake build
# (build/tools) has the real sound chips. Set MDTOOL_DLL to choose explicitly.
_DLL_CANDIDATES = [
    os.environ.get('MDTOOL_DLL', ''),
    os.path.join(ROOT, 'tools', 'bin', 'mdtool.dll'),
    os.path.join(ROOT, 'build', 'tools', 'mdtool.dll'),
]

BTN = {'UP': 0x01, 'DOWN': 0x02, 'LEFT': 0x04, 'RIGHT': 0x08,
       'B': 0x10, 'C': 0x20, 'A': 0x40, 'START': 0x80}


def _load():
    for p in _DLL_CANDIDATES:
        if p and os.path.exists(p):
            return C.CDLL(p)
    raise FileNotFoundError('mdtool.dll not found; set MDTOOL_DLL')


_dll = _load()
_dll.mdt_create.restype = C.c_void_p
_dll.mdt_create.argtypes = [C.c_char_p]
for name in ['mdt_destroy', 'mdt_power_on', 'mdt_soft_reset']:
    getattr(_dll, name).argtypes = [C.c_void_p]
_dll.mdt_set_buttons.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.mdt_run_frames.argtypes = [C.c_void_p, C.c_int]
_dll.mdt_frame_count.restype = C.c_uint64
_dll.mdt_frame_count.argtypes = [C.c_void_p]
for name in ['mdt_ram', 'mdt_zram', 'mdt_vram', 'mdt_vdp_regs', 'mdt_rom']:
    getattr(_dll, name).restype = C.POINTER(C.c_uint8)
    getattr(_dll, name).argtypes = [C.c_void_p]
for name in ['mdt_cram', 'mdt_vsram']:
    getattr(_dll, name).restype = C.POINTER(C.c_uint16)
    getattr(_dll, name).argtypes = [C.c_void_p]
_dll.mdt_rom_size.argtypes = [C.c_void_p]
_dll.mdt_set_ext.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.mdt_get_cpu.argtypes = [C.c_void_p, C.POINTER(C.c_uint32)]
_dll.mdt_get_z80.argtypes = [C.c_void_p, C.POINTER(C.c_uint32)]
_dll.mdt_frame.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_int)]
_dll.mdt_save_png.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
_dll.mdt_coverage_enable.argtypes = [C.c_void_p, C.c_int]
_dll.mdt_coverage.restype = C.POINTER(C.c_uint32)
_dll.mdt_coverage.argtypes = [C.c_void_p]
_dll.mdt_coverage_clear.argtypes = [C.c_void_p]
_dll.mdt_bp_add.argtypes = [C.c_void_p, C.c_uint32]
_dll.mdt_bp_clear.argtypes = [C.c_void_p]
_dll.mdt_bp_log.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.c_int]
_dll.mdt_watch.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
_dll.mdt_watch_log.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.c_int]
_dll.mdt_state_size.argtypes = [C.c_void_p]
_dll.mdt_save_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
_dll.mdt_load_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
_dll.mdt_audio.argtypes = [C.c_void_p, C.POINTER(C.c_int32), C.c_int]
_dll.mdt_sample_rate.restype = C.c_double
_dll.mdt_sample_rate.argtypes = [C.c_void_p]
_dll.mdt_patch16.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
_HAS_MODULES = hasattr(_dll, 'mdt_module_create')   # only in the CMake build (build/tools)
if _HAS_MODULES:
    _dll.mdt_module_create.argtypes = [C.c_void_p, C.c_char_p]
    _dll.mdt_module_configure.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_char_p]
if hasattr(_dll, 'mdt_module_can_configure'):
    _dll.mdt_module_can_configure.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_char_p]
    _dll.mdt_module_start_level.argtypes = [C.c_void_p, C.c_int, C.c_int]
    _dll.mdt_module_frames.argtypes = [C.c_void_p, C.c_int]
    _dll.mdt_module_levels.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
    _dll.mdt_module_sounds.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
    for _n in ['mdt_module_jukebox_boot', 'mdt_module_jukebox_frame', 'mdt_module_stop']:
        getattr(_dll, _n).argtypes = [C.c_void_p]
    _dll.mdt_module_play.argtypes = [C.c_void_p, C.c_int]
if hasattr(_dll, 'mdt_debug_layers'):
    _dll.mdt_debug_layers.argtypes = [C.c_void_p, C.c_int]
if hasattr(_dll, 'mdt_view'):
    _dll.mdt_view.argtypes = [C.c_void_p, C.POINTER(C.c_int)]
if hasattr(_dll, 'mdt_view_b'):
    _dll.mdt_view_b.argtypes = [C.c_void_p, C.POINTER(C.c_int)]
    _dll.mdt_frame_raw.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]


class MD:
    def __init__(self, rom=COI):
        self.h = _dll.mdt_create(rom.encode('mbcs'))
        if not self.h:
            raise RuntimeError('failed to load ' + rom)
        self.rom_path = rom
        self.buttons = 0

    def __del__(self):
        if getattr(self, 'h', None):
            _dll.mdt_destroy(self.h)
            self.h = None

    # ---- execution ----
    def reset(self):
        _dll.mdt_power_on(self.h)

    def run(self, frames=1, buttons=None):
        if buttons is not None:
            self.hold(buttons)
        _dll.mdt_run_frames(self.h, frames)

    def hold(self, buttons):
        if isinstance(buttons, str):
            buttons = sum(BTN[b] for b in buttons.split('+') if b)
        self.buttons = buttons
        _dll.mdt_set_buttons(self.h, 0, buttons)

    def hold2(self, buttons):
        """Player 2 pad (port 2)."""
        if isinstance(buttons, str):
            buttons = sum(BTN[b] for b in buttons.split('+') if b)
        _dll.mdt_set_buttons(self.h, 1, buttons)

    def press(self, buttons, frames=4, after=4):
        self.hold(buttons)
        self.run(frames)
        self.hold(0)
        self.run(after)

    @property
    def frame(self):
        return _dll.mdt_frame_count(self.h)

    # ---- memory ----
    def _arr(self, fn, n, t=C.c_uint8):
        return C.cast(fn(self.h), C.POINTER(t * n)).contents

    @property
    def ram(self):
        return self._arr(_dll.mdt_ram, 0x10000)

    @property
    def vram(self):
        return self._arr(_dll.mdt_vram, 0x10000)

    @property
    def zram(self):
        return self._arr(_dll.mdt_zram, 0x2000)

    @property
    def cram(self):
        return self._arr(_dll.mdt_cram, 64, C.c_uint16)

    @property
    def vsram(self):
        return self._arr(_dll.mdt_vsram, 64, C.c_uint16)

    @property
    def regs(self):
        return self._arr(_dll.mdt_vdp_regs, 32)

    @property
    def rom(self):
        return self._arr(_dll.mdt_rom, _dll.mdt_rom_size(self.h))

    def ram8(self, a):
        return self.ram[a & 0xFFFF]

    def ram16(self, a):
        r = self.ram; a &= 0xFFFF
        return (r[a] << 8) | r[(a + 1) & 0xFFFF]

    def ram32(self, a):
        return (self.ram16(a) << 16) | self.ram16(a + 2)

    def set8(self, a, v):
        self.ram[a & 0xFFFF] = v & 0xFF

    def set16(self, a, v):
        self.set8(a, v >> 8); self.set8(a + 1, v)

    def set32(self, a, v):
        self.set16(a, v >> 16); self.set16(a + 2, v)

    def ram_bytes(self):
        return bytes(self.ram)

    def patch16(self, a, v):
        _dll.mdt_patch16(self.h, a, v)

    def cpu(self):
        out = (C.c_uint32 * 20)()
        _dll.mdt_get_cpu(self.h, out)
        names = ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(8)] + ['pc', 'sr', 'usp', 'ssp']
        return dict(zip(names, list(out)))

    def z80(self):
        out = (C.c_uint32 * 11)()
        _dll.mdt_get_z80(self.h, out)
        return dict(zip(['af', 'bc', 'de', 'hl', 'ix', 'iy', 'sp', 'pc', 'bank', 'reset', 'busreq'], list(out)))

    # ---- video ----
    def set_ext(self, ext, sprite_limit=True):
        _dll.mdt_set_ext(self.h, ext, 1 if sprite_limit else 0)

    def png(self, path, scale=2):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        return _dll.mdt_save_png(self.h, path.encode('mbcs'), scale)

    def debug_layers(self, mask):
        """Test only: render just these layers (bit 0 plane A, bit 1 plane B, bit 2 sprites)."""
        _dll.mdt_debug_layers(self.h, mask)

    def view_b(self):
        """Per-line plane B shift for the next frame (240 values)."""
        out = (C.c_int * 240)()
        _dll.mdt_view_b(self.h, out)
        return list(out)

    def view(self):
        """Widescreen view state for the next frame: (view_shift, edge_mask_left, edge_mask_right)."""
        out = (C.c_int * 3)()
        _dll.mdt_view(self.h, out)
        return tuple(out)

    def frame_raw(self):
        """Test only: (w, h, list of raw frame-buffer codes = CRAM words as output)."""
        w = C.c_int(); h = C.c_int()
        _dll.mdt_frame_raw(self.h, None, C.byref(w), C.byref(h))
        buf = (C.c_uint16 * (w.value * h.value))()
        _dll.mdt_frame_raw(self.h, buf, C.byref(w), C.byref(h))
        return w.value, h.value, list(buf)

    def pixels(self):
        w = C.c_int(); h = C.c_int()
        _dll.mdt_frame(self.h, None, C.byref(w), C.byref(h))
        buf = (C.c_uint32 * (w.value * h.value))()
        _dll.mdt_frame(self.h, buf, C.byref(w), C.byref(h))
        return w.value, h.value, list(buf)

    # ---- analysis ----
    def coverage(self, on=True):
        _dll.mdt_coverage_enable(self.h, 1 if on else 0)

    def coverage_clear(self):
        _dll.mdt_coverage_clear(self.h)

    def covered(self, lo=0, hi=0x100000):
        """Return list of (addr, count) for executed instruction addresses in [lo, hi)."""
        cov = C.cast(_dll.mdt_coverage(self.h), C.POINTER(C.c_uint32 * 0x200000)).contents
        return [(w * 2, cov[w]) for w in range(lo >> 1, hi >> 1) if cov[w]]

    def bp(self, addr):
        _dll.mdt_bp_add(self.h, addr)

    def bp_clear(self):
        _dll.mdt_bp_clear(self.h)

    def bp_hits(self, maxn=100000):
        buf = (C.c_uint32 * (20 * maxn))()
        n = _dll.mdt_bp_log(self.h, buf, maxn)
        hits = []
        for i in range(n):
            w = buf[i * 20:(i + 1) * 20]
            regs = w[4:20]
            hits.append({'frame': w[0], 'pc': w[1], 'sr': w[2],
                         **{'d%d' % j: regs[j] for j in range(8)},
                         **{'a%d' % j: regs[8 + j] for j in range(8)}})
        return hits

    def watch(self, lo, hi=None):
        if hi is None:
            hi = lo
        _dll.mdt_watch(self.h, lo, hi)

    def unwatch(self):
        _dll.mdt_watch(self.h, 0, 0)

    def watch_hits(self, maxn=200000):
        buf = (C.c_uint32 * (4 * maxn))()
        n = _dll.mdt_watch_log(self.h, buf, maxn)
        return [{'frame': buf[i * 4], 'pc': buf[i * 4 + 1], 'addr': buf[i * 4 + 2] & 0xFFFFFF,
                 'size': buf[i * 4 + 2] >> 24, 'value': buf[i * 4 + 3]} for i in range(n)]

    def save(self):
        n = _dll.mdt_state_size(self.h)
        buf = (C.c_uint8 * n)()
        _dll.mdt_save_state(self.h, buf, n)
        return bytes(buf)

    def load(self, data):
        buf = (C.c_uint8 * len(data)).from_buffer_copy(data)
        if not _dll.mdt_load_state(self.h, buf, len(data)):
            raise RuntimeError('load_state failed')

    def audio(self):
        buf = (C.c_int32 * 4096)()
        n = _dll.mdt_audio(self.h, buf, 2048)
        return list(buf[:n * 2])

    @property
    def sample_rate(self):
        return _dll.mdt_sample_rate(self.h)


class ModuleMD(MD):
    """MD plus a game module from src/game (needs the CMake-built DLL: set MDTOOL_DLL)."""
    def __init__(self, rom, game_id):
        super().__init__(rom)
        if not _HAS_MODULES or not _dll.mdt_module_create(self.h, game_id.encode()):
            raise RuntimeError('module support not available in this DLL')
        self.info = 0

    def configure(self, ext=0, hud_anchor=True, cheats=()):
        _dll.mdt_module_configure(self.h, ext, 1 if hud_anchor else 0, ','.join(cheats).encode())

    def can_configure(self, ext=0, hud_anchor=True, cheats=()):
        """whether configure() could change the ROM now without pulling code from under the 68000 (the app
        waits for such a frame end before a width / cheat change)"""
        return bool(_dll.mdt_module_can_configure(self.h, ext, 1 if hud_anchor else 0, ','.join(cheats).encode()))

    def start_level(self, entry, variant=0):
        return _dll.mdt_module_start_level(self.h, entry, variant)

    def mrun(self, frames=1):
        self.info = _dll.mdt_module_frames(self.h, frames)
        return {'gameplay': bool(self.info & 1), 'mask_left': (self.info >> 8) & 0xFF, 'mask_right': (self.info >> 16) & 0xFF}

    def levels(self):
        buf = C.create_string_buffer(65536)
        _dll.mdt_module_levels(self.h, buf, 65536)
        return [tuple(l.split('|')) for l in buf.value.decode().splitlines()]

    def sounds(self):
        buf = C.create_string_buffer(65536)
        _dll.mdt_module_sounds(self.h, buf, 65536)
        return [(int(a), b == '1', c) for a, b, c in (l.split('|', 2) for l in buf.value.decode().splitlines())]

    def jukebox_boot(self):
        _dll.mdt_module_jukebox_boot(self.h)

    def jukebox_frame(self):
        _dll.mdt_module_jukebox_frame(self.h)

    def play(self, sid):
        _dll.mdt_module_play(self.h, sid)

    def stop(self):
        _dll.mdt_module_stop(self.h)


def ramsearch_snapshot(md):
    return bytes(md.ram)
