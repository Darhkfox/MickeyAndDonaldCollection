#include "common.h"
#include "../core/md.h"
#include <algorithm>
#include <unordered_map>

int apply_patches(MegaDrive& md, const std::vector<RomPatch>& patches) {
    md.restore_rom();
    int applied = 0;
    for (const RomPatch& p : patches) {
        bool ok = true;
        for (size_t i = 0; i < p.orig.size(); i++)
            if (md.rom16(p.addr + (uint32_t)i * 2) != p.orig[i]) { ok = false; break; }
        if (!ok) continue;
        for (size_t i = 0; i < p.repl.size(); i++) md.patch_rom16(p.addr + (uint32_t)i * 2, p.repl[i]);
        applied++;
    }
    if (applied) {
        uint16_t sum = 0;
        for (uint32_t a = 0x200; a + 1 < md.rom.size(); a += 2) sum = (uint16_t)(sum + md.rom16(a));
        md.patch_rom16(0x18E, sum);
    }
    return applied;
}

bool patches_safe(const MegaDrive& md, const std::vector<RomPatch>& patches) {
    // the ROM the patches give (applied in order on the pristine ROM, as apply_patches does)
    std::unordered_map<uint32_t, uint16_t> next;
    auto word = [&](uint32_t a) { auto i = next.find(a); return i != next.end() ? i->second : md.rom_orig16(a); };
    for (const RomPatch& p : patches) {
        bool ok = true;
        for (size_t i = 0; i < p.orig.size() && ok; i++) ok = word(p.addr + (uint32_t)i * 2) == p.orig[i];
        if (!ok) continue;
        for (size_t i = 0; i < p.repl.size(); i++) next[p.addr + (uint32_t)i * 2] = p.repl[i];
    }
    std::vector<uint32_t> changed;
    for (uint32_t a = 0x200; a + 1 < md.rom.size(); a += 2)
        if (word(a) != md.rom16(a)) changed.push_back(a);
    if (changed.empty()) return true;
    auto hit = [&](uint32_t lo, uint32_t hi) {      // a changed word in [lo, hi)
        auto i = std::lower_bound(changed.begin(), changed.end(), lo);
        return i != changed.end() && *i < hi;
    };
    const uint32_t pc = md.cpu.pc & 0xFFFFFF;
    if (hit(pc, pc + 10)) return false;
    // return addresses (jsr / bsr, interrupt frames) in the top of the stack
    const uint32_t sp = md.cpu.r[15] & 0xFFFFFF;
    if (sp >= 0xFF0000)
        for (uint32_t o = 0; o < 512 && sp + o + 3 <= 0xFFFFFF; o += 2) {
            const uint32_t a = (sp + o) & 0xFFFF;
            const uint32_t v = ((uint32_t)md.ram[a] << 24 | (uint32_t)md.ram[(a + 1) & 0xFFFF] << 16 |
                                (uint32_t)md.ram[(a + 2) & 0xFFFF] << 8 | md.ram[(a + 3) & 0xFFFF]) & 0xFFFFFF;
            if (v >= 0x200 && hit(v, v + 8)) return false;
        }
    return true;
}

bool run_until(MegaDrive& md, const std::function<bool()>& cond, int max_frames) {
    for (int i = 0; i < max_frames; i++) {
        if (cond()) return true;
        md.run_frame();
    }
    return cond();
}

bool press_until(MegaDrive& md, uint16_t button, const std::function<bool()>& cond,
                 const std::function<bool()>& idle, int hold_frames, int tries) {
    for (int t = 0; t < tries; t++) {
        md.set_pad(0, 0);
        if (!run_until(md, idle, 600)) return false;
        md.set_pad(0, button);
        for (int f = 0; f < hold_frames; f++) {
            md.run_frame();
            if (cond()) { md.set_pad(0, 0); return true; }
        }
        md.set_pad(0, 0);
        md.run_frame();
    }
    return false;
}

void freeze_68k(MegaDrive& md) {
    md.cpu.set_sr(0x2700);
    md.cpu.stopped = true;
    md.z80_busreq = false;
    md.z80_reset = false;
}

void HudLayout::build(int ext, int width) {
    const int total = width + ext * 2;
    map.assign(total, -1);
    // Default: centred copy of the original bar.
    for (int x = 0; x < width; x++) map[x + ext] = (int16_t)x;
    if (ext > 0) {
        // Fill everything with the filler pattern first, then place the segments.
        int fw = std::max(1, filler_x1 - filler_x0);
        for (int x = 0; x < total; x++) map[x] = (int16_t)(filler_x0 + ((x - ext) % fw + fw) % fw);
        for (const HudSegment& s : segments) {
            int shift = s.anchor < 0 ? -ext : s.anchor > 0 ? ext : 0;
            for (int sx = s.src_x0; sx < s.src_x1; sx++) {
                int ox = sx + ext + shift;
                if (ox >= 0 && ox < total) map[ox] = (int16_t)sx;
            }
        }
    }
    built_ext = ext;
}

void hud_remap_window_line(VDP& vdp, int line, uint8_t* plane_a, uint8_t* plane_b, const std::vector<int16_t>& map) {
    const int ext = vdp.ext;
    const int width = vdp.active_width();
    uint8_t src[VDP::kMaxWidth];
    vdp.render_window(src, line, 0, width);
    const int total = std::min<int>(width + ext * 2, (int)map.size());
    for (int x = 0; x < total; x++) {
        int sx = map[x];
        plane_a[x] = sx >= 0 && sx < width ? src[sx] : 0;
        if (x < ext || x >= ext + width) plane_b[x] = 0;
    }
}

void hud_remap_line(VDP& vdp, int line, uint8_t* plane_a, uint8_t* plane_b, HudLayout& layout) {
    if (line < layout.y0 || line >= layout.y1) return;
    const int ext = vdp.ext;
    const int width = vdp.active_width();
    if (layout.built_ext != ext) layout.build(ext, width);
    uint8_t src[VDP::kMaxWidth];
    vdp.render_window(src, line, 0, width);
    const int total = width + ext * 2;
    for (int x = 0; x < total; x++) {
        int sx = layout.map[x];
        plane_a[x] = sx >= 0 ? src[sx] : 0;
        if (layout.clear_plane_b && (x < ext || x >= ext + width)) plane_b[x] = 0;
    }
}
