// Sega 315-5313 VDP, mode 5.
#include "vdp.h"
#include <cstring>
#include <algorithm>

// CRAM is stored internally as BGR333 (9 bits).
static inline uint16_t cram_pack(uint16_t v) {
    return (uint16_t)((((v >> 9) & 7) << 6) | (((v >> 5) & 7) << 3) | ((v >> 1) & 7));
}
static inline uint16_t cram_unpack(uint16_t c) {
    return (uint16_t)((((c >> 6) & 7) << 9) | (((c >> 3) & 7) << 5) | ((c & 7) << 1));
}

VDP::VDP() { reset(); }

void VDP::reset() {
    memset(vram, 0, sizeof(vram));
    memset(cram, 0, sizeof(cram));
    memset(vsram, 0, sizeof(vsram));
    memset(reg, 0, sizeof(reg));
    memset(fb, 0, sizeof(fb));
    pending = false; code = 0; addr = 0; fill_pending = false;
    sprite_overflow = sprite_collision = false;
    vint_pending = hint_pending = false;
    hint_counter = 0;
    hv_latched = false;
    dot_overflow_prev = false;
    dma_busy_until = 0;
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
int VDP::irq_level() const {
    if (vint_pending && (reg[1] & 0x20)) return 6;
    if (hint_pending && (reg[0] & 0x10)) return 4;
    return 0;
}
void VDP::update_irq() { if (irq_changed) irq_changed(host, irq_level()); }
int VDP::acknowledge(int level) {
    if (level == 6) vint_pending = false;
    else if (level == 4) hint_pending = false;
    update_irq();
    return -1;
}
void VDP::trigger_vint() { vint_pending = true; update_irq(); }
void VDP::hint_line() {
    if (line <= active_lines()) {
        if (--hint_counter < 0) {
            hint_counter = reg[10];
            hint_pending = true;
            update_irq();
        }
    } else {
        hint_counter = reg[10];
    }
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
void VDP::write_reg(int r, uint8_t v) {
    if (r >= 24) return;
    uint8_t old = reg[r];
    reg[r] = v;
    if (r == 0) {
        if ((v & 2) && !(old & 2)) { hv_latched = false; hv_latch = read_hv(); hv_latched = true; }
        if (!(v & 2)) hv_latched = false;
    }
    if (r == 0 || r == 1) update_irq();
}

void VDP::write_ctrl(uint16_t v) {
    if (pending) {
        pending = false;
        addr = (uint16_t)((addr & 0x3FFF) | ((v & 3) << 14));
        code = (uint8_t)((code & 0x03) | ((v >> 2) & 0x3C));
        if ((code & 0x20) && (reg[1] & 0x10)) {
            switch (reg[23] >> 6) {
            case 2: fill_pending = true; break;
            case 3: do_dma_copy(); break;
            default: do_dma_68k(); break;
            }
        }
        return;
    }
    if ((v & 0xC000) == 0x8000) write_reg((v >> 8) & 0x1F, (uint8_t)v);
    else pending = true;
    addr = (uint16_t)((addr & 0xC000) | (v & 0x3FFF));
    code = (uint8_t)((code & 0x3C) | (v >> 14));
}

void VDP::write_vram16(uint16_t a, uint16_t v) {
    if (a & 1) v = (uint16_t)((v >> 8) | (v << 8));
    a &= 0xFFFE;
    vram[a] = (uint8_t)(v >> 8);
    vram[a + 1] = (uint8_t)v;
}

void VDP::write_data(uint16_t v) {
    pending = false;
    switch (code & 0x0F) {
    case 1: write_vram16(addr, v); break;
    case 3: cram[(addr >> 1) & 0x3F] = cram_pack(v); break;
    case 5: if (((addr >> 1) & 0x3F) < 40) vsram[(addr >> 1) & 0x3F] = v & 0x7FF; break;
    default: break;
    }
    addr = (uint16_t)(addr + reg[15]);
    if (fill_pending) {
        fill_pending = false;
        do_dma_fill(v);
    }
}

uint16_t VDP::read_data() {
    pending = false;
    uint16_t v = 0;
    switch (code & 0x0F) {
    case 0x0: v = vram16(addr); break;
    case 0x4: v = ((addr >> 1) & 0x3F) < 40 ? vsram[(addr >> 1) & 0x3F] : 0; break;
    case 0x8: v = cram_unpack(cram[(addr >> 1) & 0x3F]); break;
    case 0xC: v = vram[addr ^ 1]; break;
    default: break;
    }
    addr = (uint16_t)(addr + reg[15]);
    return v;
}

uint16_t VDP::read_status() {
    pending = false;
    uint16_t s = 0x3400 | 0x0200;   // FIFO always empty (writes complete instantly)
    if (vint_pending) s |= 0x80;
    if (sprite_overflow) s |= 0x40;
    if (sprite_collision) s |= 0x20;
    if (odd_frame) s |= 0x10;
    if (vblank || !display_enabled()) s |= 0x08;
    int lc = line_clock ? line_clock(host) : 0;
    if (lc >= 2560 || lc < 36) s |= 0x04;
    if (mclk_now < dma_busy_until) s |= 0x02;
    if (pal) s |= 0x01;
    sprite_overflow = sprite_collision = false;
    return s;
}

uint16_t VDP::read_hv() {
    if (hv_latched) return hv_latch;
    int lc = line_clock ? line_clock(host) : 0;
    int hc;
    if (h40()) {
        hc = lc * 210 / 3420;
        if (hc > 0xB6) hc = hc - 0xB7 + 0xE4;
    } else {
        hc = lc * 171 / 3420;
        if (hc > 0x93) hc = hc - 0x94 + 0xE9;
    }
    int vc = line;
    if (!pal) { if (vc > 0xEA) vc -= 6; }
    else      { if (vc > 0x102) vc -= 57; }
    return (uint16_t)(((vc & 0xFF) << 8) | (hc & 0xFF));
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------
int VDP::dma_rate_bytes() const {
    bool blank = vblank || !display_enabled();
    if (h40()) return blank ? 205 : 18;
    return blank ? 167 : 16;
}

void VDP::do_dma_68k() {
    uint32_t len = reg[19] | (reg[20] << 8);
    if (!len) len = 0x10000;
    uint32_t src = ((uint32_t)reg[21] << 1) | ((uint32_t)reg[22] << 9) | ((uint32_t)(reg[23] & 0x7F) << 17);
    int target = code & 0x0F;
    for (uint32_t i = 0; i < len; i++) {
        uint16_t w = dma_read ? dma_read(host, src) : 0;
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
        switch (target) {
        case 1: write_vram16(addr, w); break;
        case 3: cram[(addr >> 1) & 0x3F] = cram_pack(w); break;
        case 5: if (((addr >> 1) & 0x3F) < 40) vsram[(addr >> 1) & 0x3F] = w & 0x7FF; break;
        default: break;
        }
        addr = (uint16_t)(addr + reg[15]);
    }
    reg[19] = reg[20] = 0;
    reg[21] = (uint8_t)(src >> 1);
    reg[22] = (uint8_t)(src >> 9);
    code &= ~0x20;
    // The 68000 is frozen while the VDP owns the bus.
    int slots = target == 1 ? (int)len * 2 : (int)len;
    int mclk = (int)((int64_t)slots * 3420 / dma_rate_bytes());
    if (cpu_stall) cpu_stall(host, mclk / 7);
}

void VDP::do_dma_fill(uint16_t data) {
    uint32_t len = reg[19] | (reg[20] << 8);
    if (!len) len = 0x10000;
    int target = code & 0x0F;
    for (uint32_t i = 0; i < len; i++) {
        switch (target) {
        case 1: vram[addr ^ 1] = (uint8_t)(data >> 8); break;
        case 3: cram[(addr >> 1) & 0x3F] = cram_pack(data); break;
        case 5: if (((addr >> 1) & 0x3F) < 40) vsram[(addr >> 1) & 0x3F] = data & 0x7FF; break;
        default: break;
        }
        addr = (uint16_t)(addr + reg[15]);
    }
    reg[19] = reg[20] = 0;
    code &= ~0x20;
    dma_busy_until = mclk_now + (int64_t)len * 3420 / dma_rate_bytes();
}

void VDP::do_dma_copy() {
    uint32_t len = reg[19] | (reg[20] << 8);
    if (!len) len = 0x10000;
    uint16_t src = (uint16_t)(reg[21] | (reg[22] << 8));
    for (uint32_t i = 0; i < len; i++) {
        vram[addr] = vram[src];
        src++;
        addr = (uint16_t)(addr + reg[15]);
    }
    reg[19] = reg[20] = 0;
    reg[21] = (uint8_t)src;
    reg[22] = (uint8_t)(src >> 8);
    code &= ~0x20;
    dma_busy_until = mclk_now + (int64_t)len * 2 * 3420 / dma_rate_bytes();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
// Decode one 8-pixel row of a pattern into line-buffer format.
static inline void decode_row(const uint8_t* vram, uint16_t e, int r, uint8_t* row) {
    int tile = e & 0x7FF;
    if (e & 0x1000) r = 7 - r;
    uint32_t a = (uint32_t)tile * 32 + r * 4;
    uint8_t attr = (uint8_t)(((e & 0x8000) ? 0x80 : 0) | (((e >> 13) & 3) << 4));
    if (e & 0x800) {
        for (int i = 0; i < 4; i++) {
            uint8_t b = vram[(a + i) & 0xFFFF];
            row[7 - i * 2] = attr | (b >> 4);
            row[6 - i * 2] = attr | (b & 15);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            uint8_t b = vram[(a + i) & 0xFFFF];
            row[i * 2] = attr | (b >> 4);
            row[i * 2 + 1] = attr | (b & 15);
        }
    }
}

void VDP::pattern_row(uint16_t entry, int row, uint8_t* out8) const { decode_row(vram, entry, row, out8); }

void VDP::render_plane(uint8_t* dst, int ln, bool plane_b, int x0, int x1) {
    static const int sizes[4] = {32, 64, 32, 128};
    int cw = sizes[reg[16] & 3], ch = sizes[(reg[16] >> 4) & 3];
    if (cw == 128 && ch > 32) ch = 32;
    if (cw == 64 && ch > 64) ch = 64;
    uint32_t nt = plane_b ? ((reg[4] & 7) << 13) : ((reg[2] & 0x38) << 10);
    uint32_t hsbase = (reg[13] & 0x3F) << 10;
    int hsoff;
    switch (reg[11] & 3) {
    case 0: hsoff = 0; break;
    case 1: hsoff = (ln & 7) * 4; break;
    case 2: hsoff = (ln & ~7) * 4; break;
    default: hsoff = ln * 4; break;
    }
    int hs = vram16(hsbase + hsoff + (plane_b ? 2 : 0)) & 0x3FF;
    bool col_vs = (reg[11] & 4) != 0;
    int ncols = active_width() / 16;
    int wmask = cw * 8 - 1, hmask = ch * 8 - 1;
    int vs = vsram[plane_b ? 1 : 0] & 0x3FF;
    int last_key = -1;
    uint8_t row[8] = {};
    for (int x = x0; x < x1; x++) {
        if (col_vs) {
            int col = x >> 4;
            if (col < 0) col = 0; else if (col >= ncols) col = ncols - 1;
            vs = vsram[col * 2 + (plane_b ? 1 : 0)] & 0x3FF;
        }
        int py = (ln + vs) & hmask;
        int px = (x - hs) & wmask;
        int cell = (py >> 3) * cw + (px >> 3);
        int key = cell * 8 + (py & 7);
        if (key != last_key) {
            last_key = key;
            decode_row(vram, vram16(nt + cell * 2), py & 7, row);
        }
        dst[x - x0] = row[px & 7];
    }
}

void VDP::render_window(uint8_t* dst, int ln, int x0, int x1) {
    int cw = h40() ? 64 : 32;
    uint32_t nt = h40() ? ((reg[3] & 0x3C) << 10) : ((reg[3] & 0x3E) << 10);
    int wmask = cw * 8 - 1;
    int last_key = -1;
    uint8_t row[8] = {};
    int py = ln;
    for (int x = x0; x < x1; x++) {
        int px = x & wmask;
        int cell = (py >> 3) * cw + (px >> 3);
        if (cell != last_key) {
            last_key = cell;
            decode_row(vram, vram16(nt + cell * 2), py & 7, row);
        }
        dst[x - x0] = row[px & 7];
    }
}

void VDP::render_sprites(uint8_t* dst, int ln) {
    const bool wide = h40();
    const int W = active_width();
    const int TW = W + 2 * ext;
    uint32_t sat = wide ? ((reg[5] & 0x7E) << 9) : ((reg[5] & 0x7F) << 9);
    int max_total = wide ? 80 : 64;
    int max_line = wide ? 20 : 16;
    int max_dots = W;
    if (!sprite_limit) { max_line = max_total; max_dots = 1 << 20; }
    else if (ext) { max_line = max_line * TW / W; max_dots = TW; }

    int idx = 0, count = 0, dots = 0;
    bool nonzero_seen = false, masked = false, dot_overflow = false;
    for (int n = 0; n < max_total; n++) {
        uint32_t base = sat + idx * 8;
        int y = vram16(base) & 0x1FF;
        uint8_t size = vram[(base + 2) & 0xFFFF];
        int link = vram[(base + 3) & 0xFFFF] & 0x7F;
        int hcells = ((size >> 2) & 3) + 1, vcells = (size & 3) + 1;
        int sy = y - 128;
        if (ln >= sy && ln < sy + vcells * 8) {
            if (count >= max_line) { sprite_overflow = true; break; }
            count++;
            uint16_t attr = vram16(base + 4);
            int x = vram16(base + 6) & (sprite_x10 ? 0x3FF : 0x1FF);
            if (x == 0) { if (nonzero_seen || dot_overflow_prev) masked = true; }
            else nonzero_seen = true;
            int width = hcells * 8;
            int draw_w = width;
            if (dots + width > max_dots) { draw_w = max_dots - dots; dot_overflow = true; }
            dots += width;
            if (!masked && draw_w > 0) {
                int sx = x - 128;
                if (sprite_xoff) sx += sprite_xoff(hook_host, idx, attr, x, y);
                uint8_t hud = sprite_is_hud && sprite_is_hud(hook_host, idx) ? 1 : 0;
                int row = ln - sy;
                bool hf = (attr & 0x800) != 0;
                if (attr & 0x1000) row = vcells * 8 - 1 - row;
                int tile0 = attr & 0x7FF;
                uint8_t pattr = (uint8_t)(((attr & 0x8000) ? 0x80 : 0) | (((attr >> 13) & 3) << 4));
                const int copy = sprite_copy ? sprite_copy(hook_host, idx, attr, x, y) : 0;
                for (int pass = 0; pass < (copy ? 2 : 1); pass++) {
                    const int px0 = sx + (pass ? copy : 0) + ext - view_shift;
                    for (int c = 0; c < hcells && c * 8 < draw_w; c++) {
                        int col = hf ? (hcells - 1 - c) : c;
                        int tile = (tile0 + col * vcells + (row >> 3)) & 0x7FF;
                        uint32_t a = (uint32_t)tile * 32 + (row & 7) * 4;
                        for (int p = 0; p < 8; p++) {
                            int bx = px0 + c * 8 + p;
                            if (bx < 0 || bx >= TW) continue;
                            int pp = hf ? 7 - p : p;
                            uint8_t b = vram[(a + (pp >> 1)) & 0xFFFF];
                            uint8_t c4 = (pp & 1) ? (b & 15) : (b >> 4);
                            if (!c4) continue;
                            if (dst[bx] & 0x0F) { sprite_collision = true; continue; }
                            dst[bx] = pattr | c4;
                            spr_hud_[bx] = hud;
                        }
                    }
                }
            }
            if (dots >= max_dots) break;
        }
        if (link == 0 || link >= max_total) break;
        idx = link;
    }
    dot_overflow_prev = dot_overflow;
}

void VDP::blank_line(int ln) {
    int TW = active_width() + 2 * ext;
    fb_width = TW;
    uint16_t bg = cram[reg[7] & 0x3F];
    for (int i = 0; i < TW; i++) fb[ln][i] = bg;
}

void VDP::render_line(int ln) {
    if (ln < 0 || ln >= kMaxLines) return;
    if (!display_enabled()) { blank_line(ln); return; }
    const int W = active_width();
    const int TW = W + 2 * ext;
    fb_width = TW;
    fb_lines = active_lines();

    uint8_t la[kMaxWidth], lb[kMaxWidth], ls[kMaxWidth];
    const int vs = view_shift;
    const int vsb = view_shift_b_on ? view_shift_b[ln] : vs;
    render_plane(lb, ln, true, -ext + vsb, W + ext + vsb);

    // Window / plane A split
    int ws = 0, we = 0;
    int wvp = (reg[18] & 0x1F) * 8;
    bool vwin = (reg[18] & 0x80) ? (ln >= wvp) : (ln < wvp);
    if (vwin) { ws = -ext; we = W + ext; }
    else {
        int hp = (reg[17] & 0x1F) * 16;
        if (reg[17] & 0x80) { if (hp < W) { ws = hp; we = W + ext; } }
        else if (hp > 0) { ws = -ext; we = hp >= W ? W + ext : hp; }
    }
    if (ws >= we) render_plane(la, ln, false, -ext + vs, W + ext + vs);
    else if (vs != 0) {
        render_plane(la, ln, false, -ext + vs, W + ext + vs);   // shifted plane, window stays put
        render_window(la + (ws + ext), ln, ws, we);
    } else {
        if (ws > -ext) render_plane(la, ln, false, -ext, ws);
        render_window(la + (ws + ext), ln, ws, we);
        if (we < W + ext) render_plane(la + (we + ext), ln, false, we, W + ext);
    }

    memset(ls, 0, TW);
    memset(spr_hud_, 0, TW);
    render_sprites(ls, ln);

    if (line_hook) line_hook(hook_host, ln, la, lb, ls);
    if (debug_layers != 7) {
        if (!(debug_layers & 1)) memset(la, 0, TW);
        if (!(debug_layers & 2)) memset(lb, 0, TW);
        if (!(debug_layers & 4)) memset(ls, 0, TW);
    }

    uint16_t bgc = cram[reg[7] & 0x3F];
    uint16_t* out = fb[ln];
    if (!(reg[12] & 0x08)) {
        for (int i = 0; i < TW; i++) {
            uint8_t a = la[i], b = lb[i], s = ls[i];
            uint8_t pix;
            if ((s & 0x8F) > 0x80) pix = s;
            else if ((a & 0x8F) > 0x80) pix = a;
            else if ((b & 0x8F) > 0x80) pix = b;
            else if (s & 0x0F) pix = s;
            else if (a & 0x0F) pix = a;
            else if (b & 0x0F) pix = b;
            else { out[i] = bgc; continue; }
            out[i] = cram[pix & 0x3F];
        }
    } else {
        // Shadow / highlight mode.
        for (int i = 0; i < TW; i++) {
            uint8_t a = la[i], b = lb[i], s = ls[i];
            int inten = ((a | b) & 0x80) ? 0 : 1;
            bool s_vis = (s & 0x0F) != 0;
            bool s_op = s_vis && (s & 0x3E) == 0x3E;          // palette 3, colour 14/15
            uint8_t pix = 0;
            bool from_sprite = false;
            if (s_vis && !s_op && (s & 0x80)) { pix = s; from_sprite = true; }
            else if ((a & 0x8F) > 0x80) pix = a;
            else if ((b & 0x8F) > 0x80) pix = b;
            else if (s_vis && !s_op) { pix = s; from_sprite = true; }
            else if (a & 0x0F) pix = a;
            else if (b & 0x0F) pix = b;
            if (from_sprite) {
                if ((s & 0x80) || (s & 0x0F) == 0x0E) inten = 0;
            } else if (s_op && ((s & 0x80) || !(pix & 0x80))) {
                if ((s & 0x0F) == 0x0F) inten = 1;
                else inten = inten == 1 ? 0 : 2;
            }
            uint16_t c = pix ? cram[pix & 0x3F] : bgc;
            out[i] = (uint16_t)((inten << 9) | c);
        }
    }
    if ((reg[0] & 0x20) && !ignore_left_blank) {
        for (int i = 0; i < 8; i++) out[ext + i] = bgc;
    }
    if (edge_mask_l > 0 || edge_mask_r > 0) {
        for (int i = 0; i < TW; i++) {
            if (i >= edge_mask_l && i < TW - edge_mask_r) continue;
            out[i] = (spr_hud_[i] && (ls[i] & 0x0F)) ? cram[ls[i] & 0x3F] : 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
struct VdpState {
    uint8_t  vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[64];
    uint8_t  reg[32];
    uint8_t  pending, code, fill_pending, sprite_overflow, sprite_collision, hv_latched, dot_overflow_prev;
    uint8_t  vint_pending, hint_pending, odd_frame;
    uint16_t addr, hv_latch;
    int32_t  hint_counter;
    int64_t  dma_busy_until;
};
size_t VDP::state_size() const { return sizeof(VdpState); }
void VDP::save_state(uint8_t* dst) const {
    VdpState* s = new VdpState;
    memset(s, 0, sizeof(*s));
    memcpy(s->vram, vram, sizeof(vram));
    memcpy(s->cram, cram, sizeof(cram));
    memcpy(s->vsram, vsram, sizeof(vsram));
    memcpy(s->reg, reg, sizeof(reg));
    s->pending = pending; s->code = code; s->fill_pending = fill_pending;
    s->sprite_overflow = sprite_overflow; s->sprite_collision = sprite_collision;
    s->hv_latched = hv_latched; s->dot_overflow_prev = dot_overflow_prev;
    s->vint_pending = vint_pending; s->hint_pending = hint_pending; s->odd_frame = odd_frame;
    s->addr = addr; s->hv_latch = hv_latch; s->hint_counter = hint_counter;
    s->dma_busy_until = dma_busy_until;
    memcpy(dst, s, sizeof(*s));
    delete s;
}
void VDP::load_state(const uint8_t* src) {
    VdpState* s = new VdpState;
    memcpy(s, src, sizeof(*s));
    memcpy(vram, s->vram, sizeof(vram));
    memcpy(cram, s->cram, sizeof(cram));
    memcpy(vsram, s->vsram, sizeof(vsram));
    memcpy(reg, s->reg, sizeof(reg));
    pending = s->pending != 0; code = s->code; fill_pending = s->fill_pending != 0;
    sprite_overflow = s->sprite_overflow != 0; sprite_collision = s->sprite_collision != 0;
    hv_latched = s->hv_latched != 0; dot_overflow_prev = s->dot_overflow_prev != 0;
    vint_pending = s->vint_pending != 0; hint_pending = s->hint_pending != 0; odd_frame = s->odd_frame != 0;
    addr = s->addr; hv_latch = s->hv_latch; hint_counter = s->hint_counter;
    dma_busy_until = s->dma_busy_until;
    delete s;
    update_irq();
}
