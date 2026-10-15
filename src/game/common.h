// Helpers shared by the game modules: verified ROM patching and window-plane HUD remapping.
#pragma once
#include <cstdint>
#include <vector>
#include <initializer_list>
#include <functional>

class MegaDrive;
class VDP;

struct RomPatch {
    uint32_t addr;
    std::vector<uint16_t> orig;   // expected original words (patch skipped if they don't match)
    std::vector<uint16_t> repl;   // replacement words
};

// Restores the pristine ROM, then applies the patches and fixes the header checksum word
// ($18E) so the games' boot-time checksum test still passes. Returns the number applied.
int apply_patches(MegaDrive& md, const std::vector<RomPatch>& patches);

// Whether the ROM can go from what it holds now to what `patches` give without pulling code from under the
// 68000: no word that would change may be at the PC or behind a return address on the stack. A width or
// cheat change made while the game runs patched code waits (Session) until this holds.
bool patches_safe(const MegaDrive& md, const std::vector<RomPatch>& patches);

// Scripted-input helpers for level select / jukebox boots (run headless, as fast as possible).
bool run_until(MegaDrive& md, const std::function<bool()>& cond, int max_frames);
// Taps `button` (MdButton bits) until cond() holds: waits for idle() (game saw the pad released),
// holds up to hold_frames, releases, retries. Returns with the pad released.
bool press_until(MegaDrive& md, uint16_t button, const std::function<bool()>& cond,
                 const std::function<bool()>& idle, int hold_frames = 3, int tries = 20);
// Parks the 68000 for good (interrupts masked) and releases the Z80 bus, leaving the Z80 sound
// driver running on its own - used by the music player.
void freeze_68k(MegaDrive& md);

// HUD strip remapping for widescreen: the window-plane status bar is split into segments
// that anchor to the left edge, the centre or the right edge of the widened screen; gaps are
// filled with a repeated "filler" column span of the bar.
struct HudSegment {
    int src_x0, src_x1;   // source pixels in the original 320-px bar
    int anchor;           // -1 = left edge, 0 = centre (unchanged), 1 = right edge
};

struct HudLayout {
    int y0 = 0, y1 = 0;                 // lines covered by the bar
    std::vector<HudSegment> segments;
    int filler_x0 = 0, filler_x1 = 8;   // source span repeated to fill gaps (multiple of 8 recommended)
    bool clear_plane_b = true;          // hide plane B behind the bar in the extension
    // output x (0 .. 320 + 2*ext) -> source x (0..319), built by build()
    std::vector<int16_t> map;
    int built_ext = -1;
    void build(int ext, int width = 320);
};

// Rewrites a line's plane-A buffer from the window plane using the layout's map.
void hud_remap_line(VDP& vdp, int line, uint8_t* plane_a, uint8_t* plane_b, HudLayout& layout);

// Per-pixel variant: map[x] (x = 0 .. width+2*ext-1) = source pixel of the original bar, or -1
// for transparent. Plane B is cleared in the extension (the bar hides it in the original view).
void hud_remap_window_line(VDP& vdp, int line, uint8_t* plane_a, uint8_t* plane_b, const std::vector<int16_t>& map);
