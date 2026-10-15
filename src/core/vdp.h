// Sega 315-5313 VDP (Mega Drive video display processor), mode 5 only.
// Supports rendering extra columns on both sides of the display (widescreen).
#pragma once
#include <cstdint>
#include <cstddef>

class VDP {
public:
    static constexpr int kMaxWidth = 640;     // max output width incl. widescreen extension
    static constexpr int kMaxLines = 240;

    VDP();
    void reset();

    // ---- CPU ports ----------------------------------------------------------
    void     write_data(uint16_t v);
    void     write_ctrl(uint16_t v);
    uint16_t read_data();
    uint16_t read_status();
    uint16_t read_hv();

    // ---- host interface -----------------------------------------------------
    // DMA source reads (68000 bus, word address).
    uint16_t (*dma_read)(void* host, uint32_t addr) = nullptr;
    // Current position within the scanline in master clocks (0..3419).
    int      (*line_clock)(void* host) = nullptr;
    // 68000 stall for DMA (68000 cycles).
    void     (*cpu_stall)(void* host, int cycles68) = nullptr;
    // Interrupt level changed (call cpu.set_irq).
    void     (*irq_changed)(void* host, int level) = nullptr;
    void*    host = nullptr;

    // Timing state maintained by the system.
    int  line = 0;              // current scanline (0..261)
    bool vblank = false;
    bool odd_frame = false;
    bool pal = false;
    int64_t mclk_now = 0;       // absolute master clock (set by the system before port access)
    int64_t dma_busy_until = 0;

    // Interrupts
    bool vint_pending = false;
    bool hint_pending = false;
    int  hint_counter = 0;
    int  irq_level() const;     // highest enabled pending interrupt level
    int  acknowledge(int level);// CPU acknowledged interrupt `level`
    void update_irq();
    void trigger_vint();        // at start of vblank
    void hint_line();           // called once per line at hblank: decrements / reloads counter

    // ---- rendering ----------------------------------------------------------
    // Extra pixels rendered on each side (0 = original width).
    int  ext = 0;
    bool sprite_limit = true;   // hardware sprites-per-line / dots-per-line limits
    bool ignore_left_blank = false; // ignore reg0 bit5 (left column blanking) - used in widescreen
    // Output columns blanked at the left/right edge (level bounds in widescreen); HUD sprites stay.
    int  edge_mask_l = 0, edge_mask_r = 0;
    // Widescreen view shift: planes and sprites are sampled `view_shift` pixels further right
    // (negative = left) than the game's own camera, the window plane stays in place. Lets a module
    // keep the widened view inside the level without touching the game's camera.
    int  view_shift = 0;
    // Optional per-line shift for plane B (used instead of view_shift when view_shift_b_on): a background that
    // scrolls slower than the camera must also be shifted less, or it drifts while the view slides.
    bool    view_shift_b_on = false;
    int16_t view_shift_b[kMaxLines] = {};
    // Read sprite X as 10 bits (screen x up to 895): lets a patched game place sprites past the
    // 9-bit limit of the real chip when the view is widened and shifted.
    bool sprite_x10 = false;
    // Draw this sprite a second time, this many px further right (0 = no copy) - a game module can show a
    // repeating background object twice when the widened view is wider than the game's repeat distance.
    int (*sprite_copy)(void* host, int index, uint16_t attr, int x, int y) = nullptr;
    uint8_t debug_layers = 7;   // test tool only: bit 0 plane A, bit 1 plane B, bit 2 sprites

    // Optional hook: horizontal offset added to a sprite's screen X (HUD re-anchoring).
    // index = sprite table index, attr = pattern word, x/y = raw table coordinates.
    int  (*sprite_xoff)(void* host, int index, uint16_t attr, int x, int y) = nullptr;
    // Optional hook: called after a line's layers are rendered (before composition) so a game
    // module can patch the plane/sprite line buffers (index = x + ext). Pixel format:
    // bit7 priority, bits5-4 palette, bits3-0 colour (0 = transparent).
    void (*line_hook)(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) = nullptr;
    // Optional hook: sprites for which it returns true are HUD elements (exempt from edge masks).
    bool (*sprite_is_hud)(void* host, int index) = nullptr;
    void* hook_host = nullptr;   // context passed to sprite_xoff / line_hook / sprite_is_hud
    uint32_t hook_flags = 0;     // free for the installer of the hooks

    void render_line(int ln);   // render scanline into framebuffer
    void blank_line(int ln);    // output backdrop colour (display disabled)

    int  active_width() const { return (reg[12] & 0x81) ? 320 : 256; }
    int  active_lines() const { return (reg[1] & 0x08) ? 240 : 224; }
    bool h40() const { return (reg[12] & 0x81) != 0; }
    bool display_enabled() const { return (reg[1] & 0x40) != 0; }

    // Framebuffer of 11-bit colour codes: (intensity << 9) | BGR333,
    // intensity 0 = normal, 1 = shadow, 2 = highlight.
    uint16_t fb[kMaxLines][kMaxWidth];
    int  fb_width = 320;        // width of the last rendered frame (active + 2*ext)
    int  fb_lines = 224;

    // ---- memory -------------------------------------------------------------
    uint8_t  vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[64];
    uint8_t  reg[32];

    // command state
    bool     pending = false;
    uint8_t  code = 0;
    uint16_t addr = 0;
    bool     fill_pending = false;
    bool     sprite_overflow = false;
    bool     sprite_collision = false;
    uint16_t hv_latch = 0;
    bool     hv_latched = false;
    bool     dot_overflow_prev = false;

    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

    uint16_t vram16(uint32_t a) const { a &= 0xFFFE; return (uint16_t)((vram[a] << 8) | vram[a + 1]); }
    // One 8-pixel row of the pattern a name-table entry points at, in line-buffer format
    // (bit 7 priority, bits 4-5 palette, bits 0-3 colour) - for game modules that draw plane cells.
    void pattern_row(uint16_t entry, int row, uint8_t* out8) const;

    // Span renderers (screen x in [x0, x1), may be negative / beyond the active width).
    // Output is in line-buffer format; public so game line hooks can re-render regions.
    void render_plane(uint8_t* dst, int ln, bool plane_b, int x0, int x1);
    void render_window(uint8_t* dst, int ln, int x0, int x1);

private:
    void do_dma_68k();
    void do_dma_fill(uint16_t data);
    void do_dma_copy();
    void write_reg(int r, uint8_t v);
    void write_vram16(uint16_t a, uint16_t v);
    void render_sprites(uint8_t* dst, int ln);
    uint8_t spr_hud_[kMaxWidth];
    int  dma_rate_bytes() const;
};
