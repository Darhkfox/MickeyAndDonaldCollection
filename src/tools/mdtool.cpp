// mdtool.dll - headless Mega Drive core with a C API for Python-driven analysis
// (screenshots, RAM/VRAM access, PC coverage, breakpoint logs, RAM write watches).
#include "../core/md.h"
#include "../game/game.h"
#include <memory>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <iterator>

#define API extern "C" __declspec(dllexport)

namespace {
struct BpHit { uint32_t frame, pc, sr, pad; uint32_t regs[16]; };
struct WatchHit { uint32_t frame, pc, addr, value; };

struct Tool {
    MegaDrive md;
    uint16_t buttons[2] = {0, 0};
    std::vector<uint32_t> coverage;      // execution count per ROM word
    bool cov_on = false;
    std::vector<BpHit> bp_log;
    std::vector<WatchHit> watch_log;
    uint32_t watch_lo = 0, watch_hi = 0;
    size_t   log_limit = 200000;
    std::vector<uint32_t> palette;       // 2048 entry colour LUT
    std::unique_ptr<GameModule> module;  // optional game module under test
    GameConfig cfg;
    FrameInfo info;
};

void build_palette(std::vector<uint32_t>& lut) {
    static const int lv[8] = {0, 52, 87, 116, 144, 172, 206, 255};
    static const int sh[8] = {0, 29, 52, 70, 87, 101, 116, 130};
    static const int hi[8] = {130, 144, 158, 172, 187, 206, 228, 255};
    lut.assign(2048, 0xFF000000);
    for (int in = 0; in < 3; in++)
        for (int c = 0; c < 512; c++) {
            const int* t = in == 0 ? lv : in == 1 ? sh : hi;
            int r = t[c & 7], g = t[(c >> 3) & 7], b = t[(c >> 6) & 7];
            lut[(in << 9) | c] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
}

void trace_cb(void* ctx, uint32_t pc) {
    Tool* t = (Tool*)ctx;
    uint32_t w = (pc & 0xFFFFFF) >> 1;
    if (w < t->coverage.size()) t->coverage[w]++;
}

// ---- minimal PNG writer (stored deflate) ----
uint32_t crc_table[256];
void crc_init() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
bool write_png(const char* path, const uint32_t* px, int w, int h, int scale) {
    static bool inited = false;
    if (!inited) { crc_init(); inited = true; }
    int W = w * scale, H = h * scale;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(W * 3 + 1) * H);
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        const uint32_t* row = px + (size_t)(y / scale) * w;
        for (int x = 0; x < W; x++) {
            uint32_t c = row[x / scale];
            raw.push_back((uint8_t)(c >> 16)); raw.push_back((uint8_t)(c >> 8)); raw.push_back((uint8_t)c);
        }
    }
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, W); be32(ihdr, H);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}
}  // namespace

API void* mdt_create(const char* rom_path) {
    std::ifstream f(rom_path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Tool* t = new Tool;
    if (!t->md.load_rom(data)) { delete t; return nullptr; }
    build_palette(t->palette);
    t->md.power_on();
    return t;
}
API void mdt_destroy(void* h) { delete (Tool*)h; }
API void mdt_power_on(void* h) { ((Tool*)h)->md.power_on(); }
API void mdt_soft_reset(void* h) { ((Tool*)h)->md.soft_reset(); }
API void mdt_set_buttons(void* h, int port, int buttons) { ((Tool*)h)->buttons[port & 1] = (uint16_t)buttons; }
API void mdt_run_frames(void* h, int n) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < n; i++) {
        t->md.set_pad(0, t->buttons[0]);
        t->md.set_pad(1, t->buttons[1]);
        t->md.run_frame();
    }
}
API uint64_t mdt_frame_count(void* h) { return ((Tool*)h)->md.frame_count; }
API uint8_t* mdt_ram(void* h) { return ((Tool*)h)->md.ram; }
API uint8_t* mdt_zram(void* h) { return ((Tool*)h)->md.zram; }
API uint8_t* mdt_vram(void* h) { return ((Tool*)h)->md.vdp.vram; }
API uint16_t* mdt_cram(void* h) { return ((Tool*)h)->md.vdp.cram; }
API uint16_t* mdt_vsram(void* h) { return ((Tool*)h)->md.vdp.vsram; }
API uint8_t* mdt_vdp_regs(void* h) { return ((Tool*)h)->md.vdp.reg; }
API uint8_t* mdt_rom(void* h) { return ((Tool*)h)->md.rom.data(); }
API int mdt_rom_size(void* h) { return (int)((Tool*)h)->md.rom.size(); }
API void mdt_set_ext(void* h, int ext, int sprite_limit) {
    Tool* t = (Tool*)h;
    t->md.vdp.ext = ext;
    t->md.vdp.sprite_limit = sprite_limit != 0;
    t->md.vdp.ignore_left_blank = ext > 0;
}
API void mdt_get_cpu(void* h, uint32_t* out) {  // d0-d7 a0-a7 pc sr usp ssp
    M68k& c = ((Tool*)h)->md.cpu;
    for (int i = 0; i < 16; i++) out[i] = c.r[i];
    out[16] = c.pc; out[17] = c.get_sr(); out[18] = c.usp; out[19] = c.ssp;
}
API void mdt_get_z80(void* h, uint32_t* out) {
    Z80& z = ((Tool*)h)->md.z80;
    out[0] = z.af; out[1] = z.bc; out[2] = z.de; out[3] = z.hl; out[4] = z.ix; out[5] = z.iy;
    out[6] = z.sp; out[7] = z.pc; out[8] = ((Tool*)h)->md.z80_bank;
    out[9] = ((Tool*)h)->md.z80_reset; out[10] = ((Tool*)h)->md.z80_busreq;
}
API void mdt_debug_layers(void* h, int mask) { ((Tool*)h)->md.vdp.debug_layers = (uint8_t)mask; }
// Widescreen view state the next frame is drawn with: view shift, left / right edge masks.
API void mdt_view(void* h, int* out) {
    const VDP& v = ((Tool*)h)->md.vdp;
    out[0] = v.view_shift; out[1] = v.edge_mask_l; out[2] = v.edge_mask_r;
}
// Per-line plane B shift (240 lines; the plain view shift when the module does not set one).
API void mdt_view_b(void* h, int* out) {
    const VDP& v = ((Tool*)h)->md.vdp;
    for (int i = 0; i < VDP::kMaxLines; i++) out[i] = v.view_shift_b_on ? v.view_shift_b[i] : v.view_shift;
}

// Raw frame-buffer codes (the CRAM words the VDP output, before any palette conversion).
API int mdt_frame_raw(void* h, uint16_t* out, int* w, int* hgt) {
    VDP& v = ((Tool*)h)->md.vdp;
    *w = v.fb_width; *hgt = v.fb_lines;
    if (out)
        for (int y = 0; y < v.fb_lines; y++)
            for (int x = 0; x < v.fb_width; x++) out[y * v.fb_width + x] = v.fb[y][x];
    return 1;
}

API int mdt_frame(void* h, uint32_t* out, int* w, int* hgt) {
    Tool* t = (Tool*)h;
    VDP& v = t->md.vdp;
    *w = v.fb_width; *hgt = v.fb_lines;
    if (out) {
        for (int y = 0; y < v.fb_lines; y++)
            for (int x = 0; x < v.fb_width; x++) out[y * v.fb_width + x] = t->palette[v.fb[y][x] & 0x7FF];
    }
    return 1;
}
API int mdt_save_png(void* h, const char* path, int scale) {
    Tool* t = (Tool*)h;
    int w, hh;
    mdt_frame(h, nullptr, &w, &hh);
    std::vector<uint32_t> px((size_t)w * hh);
    mdt_frame(h, px.data(), &w, &hh);
    return write_png(path, px.data(), w, hh, scale < 1 ? 1 : scale) ? 1 : 0;
}
API int mdt_write_png(const char* path, const uint32_t* px, int w, int h, int scale) {
    return write_png(path, px, w, h, scale) ? 1 : 0;
}

// Coverage: counts executions per ROM word (first 4 MB).
API void mdt_coverage_enable(void* h, int on) {
    Tool* t = (Tool*)h;
    t->cov_on = on != 0;
    if (on) {
        if (t->coverage.empty()) t->coverage.assign(0x200000, 0);
        t->md.cpu.trace_fn = trace_cb;
        t->md.cpu.trace_ctx = t;
    } else {
        t->md.cpu.trace_fn = nullptr;
    }
}
API uint32_t* mdt_coverage(void* h) {
    Tool* t = (Tool*)h;
    if (t->coverage.empty()) t->coverage.assign(0x200000, 0);
    return t->coverage.data();
}
API void mdt_coverage_clear(void* h) {
    Tool* t = (Tool*)h;
    std::fill(t->coverage.begin(), t->coverage.end(), 0);
}

// Breakpoints: log CPU state whenever PC reaches addr (execution continues).
API void mdt_bp_add(void* h, uint32_t addr) {
    Tool* t = (Tool*)h;
    t->md.add_pc_hook(addr, [t](MegaDrive& md) {
        if (t->bp_log.size() >= t->log_limit) return;
        BpHit b;
        b.frame = (uint32_t)md.frame_count; b.pc = md.cpu.pc; b.sr = md.cpu.get_sr(); b.pad = 0;
        memcpy(b.regs, md.cpu.r, sizeof(b.regs));
        t->bp_log.push_back(b);
    });
}
API void mdt_bp_clear(void* h) { Tool* t = (Tool*)h; t->md.clear_pc_hooks(); t->bp_log.clear(); }
API int mdt_bp_log(void* h, uint32_t* out, int max) {   // 20 words per hit
    Tool* t = (Tool*)h;
    int n = (int)std::min<size_t>(max, t->bp_log.size());
    if (out) memcpy(out, t->bp_log.data(), (size_t)n * sizeof(BpHit));
    t->bp_log.erase(t->bp_log.begin(), t->bp_log.begin() + n);
    return n;
}

// RAM write watch on [lo, hi] (68000 addresses, e.g. 0xFFxxxx).
API void mdt_watch(void* h, uint32_t lo, uint32_t hi) {
    Tool* t = (Tool*)h;
    t->watch_lo = lo & 0xFFFFFF; t->watch_hi = hi & 0xFFFFFF;
    if (lo == 0 && hi == 0) {
        t->md.enable_ram_watch(false);
        t->md.on_ram_write = nullptr;
        return;
    }
    t->md.on_ram_write = [t](MegaDrive& md, uint32_t a, uint32_t v, int size) {
        a |= 0xFF0000;
        uint32_t end = a + size - 1;
        if (end < t->watch_lo || a > t->watch_hi) return;
        if (t->watch_log.size() >= t->log_limit) return;
        t->watch_log.push_back({(uint32_t)md.frame_count, md.cpu.ppc, a | ((uint32_t)size << 24), v});
    };
    t->md.enable_ram_watch(true);
}
API int mdt_watch_log(void* h, uint32_t* out, int max) {  // 4 words per hit: frame, pc, addr|size<<24, value
    Tool* t = (Tool*)h;
    int n = (int)std::min<size_t>(max, t->watch_log.size());
    if (out) memcpy(out, t->watch_log.data(), (size_t)n * sizeof(WatchHit));
    t->watch_log.erase(t->watch_log.begin(), t->watch_log.begin() + n);
    return n;
}

API int mdt_state_size(void* h) { return (int)((Tool*)h)->md.save_state().size(); }
API int mdt_save_state(void* h, uint8_t* out, int max) {
    std::vector<uint8_t> s = ((Tool*)h)->md.save_state();
    if ((int)s.size() > max) return -(int)s.size();
    memcpy(out, s.data(), s.size());
    return (int)s.size();
}
API int mdt_load_state(void* h, const uint8_t* data, int n) {
    std::vector<uint8_t> s(data, data + n);
    return ((Tool*)h)->md.load_state(s) ? 1 : 0;
}
API int mdt_audio(void* h, int32_t* out, int max_frames) {
    MegaDrive& md = ((Tool*)h)->md;
    int n = std::min(max_frames, md.audio_frames);
    if (out) memcpy(out, md.audio.data(), (size_t)n * 2 * sizeof(int32_t));
    return n;
}
API double mdt_sample_rate(void* h) { return ((Tool*)h)->md.sample_rate(); }
API void mdt_patch16(void* h, uint32_t addr, uint32_t v) { ((Tool*)h)->md.patch_rom16(addr, (uint16_t)v); }

// ---- game module testing -----------------------------------------------------------
API int mdt_module_create(void* h, const char* id) {
    Tool* t = (Tool*)h;
    std::string s = id;
    if (s == "coi") t->module.reset(create_coi_module());
    else if (s == "qs") t->module.reset(create_qs_module());
    else if (s == "woi") t->module.reset(create_woi_module());
    else return 0;
    return 1;
}
// cheats: comma separated keys
API void mdt_module_configure(void* h, int ext, int hud_anchor, const char* cheats) {
    Tool* t = (Tool*)h;
    if (!t->module) return;
    t->cfg = GameConfig();
    t->cfg.ext = ext;
    t->cfg.hud_anchor = hud_anchor != 0;
    std::stringstream ss(cheats ? cheats : "");
    std::string k;
    while (std::getline(ss, k, ',')) if (!k.empty()) t->cfg.cheats[k] = true;
    t->md.vdp.ext = ext;
    t->md.vdp.sprite_limit = ext == 0;
    t->md.vdp.ignore_left_blank = ext > 0;
    t->module->configure(t->md, t->cfg);
}
// Whether mdt_module_configure(ext, hud_anchor, cheats) could be applied now without changing ROM code the
// 68000 is in or returns into (the app's session waits for such a frame; see patches_safe).
API int mdt_module_can_configure(void* h, int ext, int hud_anchor, const char* cheats) {
    Tool* t = (Tool*)h;
    if (!t->module) return 1;
    GameConfig c;
    c.ext = ext;
    c.hud_anchor = hud_anchor != 0;
    std::stringstream ss(cheats ? cheats : "");
    std::string k;
    while (std::getline(ss, k, ',')) if (!k.empty()) c.cheats[k] = true;
    return t->module->can_configure(t->md, c) ? 1 : 0;
}
API int mdt_module_start_level(void* h, int entry, int variant) {
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    int ok = t->module->start_level(t->md, t->cfg, entry, variant) ? 1 : 0;
    t->module->configure(t->md, t->cfg);
    return ok;
}
// Runs frames through the module hooks with the current buttons. Returns info packed:
// bit0 gameplay, bits 8-15 mask_left, bits 16-23 mask_right.
API int mdt_module_frames(void* h, int n) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < n; i++) {
        t->md.set_pad(0, t->buttons[0]);
        t->md.set_pad(1, t->buttons[1]);
        if (t->module) t->module->pre_frame(t->md, t->cfg);
        t->md.run_frame();
        t->info = FrameInfo();
        if (t->module) t->module->post_frame(t->md, t->cfg, t->info);
    }
    return (t->info.gameplay ? 1 : 0) | ((t->info.mask_left & 0xFF) << 8) | ((t->info.mask_right & 0xFF) << 16);
}
API int mdt_module_levels(void* h, char* out, int max) {   // "group|name\n" lines
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    std::string s;
    for (auto& l : t->module->levels()) s += l.group + "|" + l.name + "|" + std::to_string(l.variants) + "\n";
    if (out && max > 0) { strncpy(out, s.c_str(), max - 1); out[max - 1] = 0; }
    return (int)t->module->levels().size();
}
API int mdt_module_sounds(void* h, char* out, int max) {   // "id|music|name\n" lines
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    std::string s;
    for (auto& e : t->module->sounds()) s += std::to_string(e.id) + "|" + (e.music ? "1" : "0") + "|" + e.name + "\n";
    if (out && max > 0) { strncpy(out, s.c_str(), max - 1); out[max - 1] = 0; }
    return (int)t->module->sounds().size();
}
API void mdt_module_jukebox_boot(void* h) { Tool* t = (Tool*)h; if (t->module) t->module->jukebox_boot(t->md); }
API void mdt_module_jukebox_frame(void* h) { Tool* t = (Tool*)h; if (t->module) t->module->jukebox_frame(t->md); }
API void mdt_module_play(void* h, int id) { Tool* t = (Tool*)h; if (t->module) t->module->play_sound(t->md, id); }
API void mdt_module_stop(void* h) { Tool* t = (Tool*)h; if (t->module) t->module->stop_sound(t->md); }
