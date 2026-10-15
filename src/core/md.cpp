// Mega Drive system bus and scheduler.
#include "md.h"
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Callback trampolines
// ---------------------------------------------------------------------------
static uint8_t  cb_r8(void* c, uint32_t a) { return ((MegaDrive*)c)->read8(a); }
static uint16_t cb_r16(void* c, uint32_t a) { return ((MegaDrive*)c)->read16(a); }
static void     cb_w8(void* c, uint32_t a, uint8_t v) { ((MegaDrive*)c)->write8(a, v); }
static void     cb_w16(void* c, uint32_t a, uint16_t v) { ((MegaDrive*)c)->write16(a, v); }
static int      cb_ack(void* c, int level) { return ((MegaDrive*)c)->vdp.acknowledge(level); }
static uint8_t  cb_zr(void* c, uint16_t a) { return ((MegaDrive*)c)->z80_read(a); }
static void     cb_zw(void* c, uint16_t a, uint8_t v) { ((MegaDrive*)c)->z80_write(a, v); }
static uint16_t cb_dma_read(void* c, uint32_t a) {
    MegaDrive* md = (MegaDrive*)c;
    a &= 0xFFFFFE;
    if (a < 0x400000) return md->rom16(a);
    if (a >= 0xE00000) return md->ram16(a);
    return 0;
}
static int  cb_line_clock(void* c) { return ((MegaDrive*)c)->line_clock(); }
static void cb_stall(void* c, int cycles) { ((MegaDrive*)c)->cpu.add_cycles(cycles); }
static void cb_irq(void* c, int level) { ((MegaDrive*)c)->cpu.set_irq(level); }

MegaDrive::MegaDrive() {
    cpu.ctx = this;
    cpu.rd8 = cb_r8; cpu.rd16 = cb_r16; cpu.wr8 = cb_w8; cpu.wr16 = cb_w16;
    cpu.irq_ack = cb_ack;
    cpu.hook_fn = hook_dispatch;
    cpu.hook_ctx = this;
    z80.ctx = this;
    z80.read = cb_zr;
    z80.write = cb_zw;
    vdp.host = this;
    vdp.dma_read = cb_dma_read;
    vdp.line_clock = cb_line_clock;
    vdp.cpu_stall = cb_stall;
    vdp.irq_changed = cb_irq;
    memset(ram, 0, sizeof(ram));
    memset(zram, 0, sizeof(zram));
    audio.reserve(4096);
}

MegaDrive::~MegaDrive() {}

bool MegaDrive::load_rom(const std::vector<uint8_t>& data) {
    if (data.size() < 0x200) return false;
    size_t sz = 0x10000;
    while (sz < data.size()) sz <<= 1;          // pad to a power of two for mirroring
    rom.assign(sz, 0xFF);
    memcpy(rom.data(), data.data(), data.size());
    rom_orig = rom;
    map_pages();
    return true;
}

void MegaDrive::patch_rom16(uint32_t a, uint16_t v) {
    a &= 0x3FFFFE;
    if (a + 1 >= rom.size()) return;
    rom[a] = (uint8_t)(v >> 8);
    rom[a + 1] = (uint8_t)v;
}

void MegaDrive::restore_rom() { memcpy(rom.data(), rom_orig.data(), rom.size()); }

void MegaDrive::map_pages() {
    for (int p = 0; p < 256; p++) { cpu.page_read[p] = nullptr; cpu.page_write[p] = nullptr; }
    if (!rom.empty()) {
        for (int p = 0; p < 0x40; p++) {
            size_t off = ((size_t)p << 16) % rom.size();
            cpu.page_read[p] = &rom[off];
        }
    }
    for (int p = 0xE0; p < 0x100; p++) {
        cpu.page_read[p] = ram;
        cpu.page_write[p] = ram_watch ? nullptr : ram;
    }
}

void MegaDrive::enable_ram_watch(bool on) {
    ram_watch = on;
    map_pages();
}

void MegaDrive::power_on() {
    memset(module_state, 0, sizeof(module_state));
    memset(ram, 0, sizeof(ram));
    memset(zram, 0, sizeof(zram));
    vdp.reset();
    vdp.pal = pal;
    ym.reset();
    psg.reset();
    z80.reset();
    z80_reset = true;
    z80_busreq = false;
    z80_bank = 0;
    io_data[0] = io_data[1] = io_data[2] = 0x7F;
    io_ctrl[0] = io_ctrl[1] = io_ctrl[2] = 0;
    m68_clock = z80_clock = audio_clock = 0;
    frame_count = 0;
    cpu.reset();
}

void MegaDrive::soft_reset() {
    z80.reset();
    ym.reset();
    z80_reset = true;
    z80_busreq = false;
    cpu.reset();
}

void MegaDrive::add_pc_hook(uint32_t addr, std::function<void(MegaDrive&)> fn) {
    pc_hooks[addr & 0xFFFFFF] = std::move(fn);
    cpu.set_hook(addr, true);
}

void MegaDrive::clear_pc_hooks() {
    pc_hooks.clear();
    cpu.clear_hooks();
}

void MegaDrive::hook_dispatch(void* ctx, uint32_t pc) {
    MegaDrive* md = (MegaDrive*)ctx;
    auto it = md->pc_hooks.find(pc & 0xFFFFFF);
    if (it != md->pc_hooks.end()) it->second(*md);
}

int MegaDrive::line_clock() const {
    int64_t t = (in_z80 ? now_z80() : now_68k()) - line_start;
    if (t < 0) t = 0;
    if (t >= kMclkPerLine) t = kMclkPerLine - 1;
    return (int)t;
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------
void MegaDrive::run_until(int64_t target) {
    // Interleave both CPUs in slices of a third of a line.
    while (m68_clock < target || z80_clock < target) {
        int64_t slice_end = std::min(target, std::min(m68_clock, z80_clock) + 1140);
        if (m68_clock < slice_end) {
            int cycles = (int)((slice_end - m68_clock + 6) / 7);
            int done = cpu.run(cycles);
            m68_clock += (int64_t)done * 7;
        }
        if (!z80_reset && !z80_busreq) {
            if (z80_clock < slice_end) {
                in_z80 = true;
                int done = z80.run((int)((slice_end - z80_clock + 14) / 15));
                in_z80 = false;
                z80_clock += (int64_t)done * 15;
            }
        } else if (z80_clock < slice_end) {
            z80_clock = slice_end;
        }
    }
}

void MegaDrive::sync_audio(int64_t t) {
    int64_t n = (t - audio_clock) / kMclkPerSample;
    if (n <= 0) return;
    size_t need = (size_t)(audio_frames + n) * 2;
    if (audio.size() < need) audio.resize(need + 256);
    if (ym_tmp.size() < (size_t)n * 2) ym_tmp.resize((size_t)n * 2);
    if (psg_tmp.size() < (size_t)n) psg_tmp.resize((size_t)n);
    ym.generate(ym_tmp.data(), (int)n);
    double clock = pal ? kMclkPal : kMclkNtsc;
    psg.generate(psg_tmp.data(), (int)n, clock / 15.0, sample_rate());
    int32_t* out = &audio[(size_t)audio_frames * 2];
    for (int64_t i = 0; i < n; i++) {
        int32_t p = (int32_t)(psg_tmp[i] * psg_gain);
        out[i * 2] = (int32_t)(ym_tmp[i * 2] * fm_gain) + p;
        out[i * 2 + 1] = (int32_t)(ym_tmp[i * 2 + 1] * fm_gain) + p;
    }
    audio_frames += (int)n;
    audio_clock += n * kMclkPerSample;
}

void MegaDrive::run_frame() {
    if (on_frame_start) on_frame_start(*this);
    const int lines = lines_per_frame();
    audio_frames = 0;
    vdp.pal = pal;
    for (int ln = 0; ln < lines; ln++) {
        line_start = (int64_t)ln * kMclkPerLine;
        vdp.line = ln;
        vdp.mclk_now = line_start;
        int active = vdp.active_lines();
        if (ln == 0) vdp.vblank = false;
        if (ln < active) vdp.render_line(ln);
        if (ln == active) {
            vdp.vblank = true;
            if (on_vblank) on_vblank(*this);
            vdp.trigger_vint();
            z80.set_irq(true);
            z80_int_clear_line = ln + 1;
        } else if (ln == z80_int_clear_line) {
            z80.set_irq(false);
            z80_int_clear_line = -1;
        }
        run_until(line_start + 2560);
        vdp.mclk_now = line_start + 2560;
        vdp.hint_line();
        run_until(line_start + kMclkPerLine);
    }
    const int64_t frame_len = (int64_t)lines * kMclkPerLine;
    sync_audio(frame_len);
    m68_clock -= frame_len;
    z80_clock -= frame_len;
    audio_clock -= frame_len;
    vdp.dma_busy_until = std::max<int64_t>(0, vdp.dma_busy_until - frame_len);
    vdp.odd_frame = !vdp.odd_frame;
    frame_count++;
    if (on_frame_end) on_frame_end(*this);
}

// ---------------------------------------------------------------------------
// Sound chip access (time-synchronised)
// ---------------------------------------------------------------------------
void MegaDrive::ym_write(int port, uint8_t v, int64_t t) {
    sync_audio(t);
    ym.write(port, v);
}
uint8_t MegaDrive::ym_read(int64_t t) {
    sync_audio(t);
    return ym.read_status();
}

// ---------------------------------------------------------------------------
// I/O ports
// ---------------------------------------------------------------------------
uint8_t MegaDrive::pad_read(int p) {
    uint8_t ctrl = io_ctrl[p], data = io_data[p];
    bool th = (ctrl & 0x40) ? (data & 0x40) != 0 : true;
    uint16_t b = pad_buttons[p];
    uint8_t in;
    if (th) in = (uint8_t)(0x40 | (~b & 0x3F));
    else    in = (uint8_t)((~b & 0x03) | ((~b >> 2) & 0x30));
    return (uint8_t)((data & ctrl & 0x7F) | (in & ~ctrl & 0x7F) | (data & 0x80));
}

uint8_t MegaDrive::io_read(uint32_t a) {
    switch (a & 0x1F) {
    case 0x01: return (uint8_t)((overseas ? 0x80 : 0) | (pal ? 0x40 : 0) | 0x20);
    case 0x03: return pad_read(0);
    case 0x05: return pad_read(1);
    case 0x07: return io_data[2];
    case 0x09: return io_ctrl[0];
    case 0x0B: return io_ctrl[1];
    case 0x0D: return io_ctrl[2];
    case 0x0F: case 0x15: case 0x1B: return 0xFF;
    default: return 0;
    }
}

void MegaDrive::io_write(uint32_t a, uint8_t v) {
    switch (a & 0x1F) {
    case 0x03: io_data[0] = v; break;
    case 0x05: io_data[1] = v; break;
    case 0x07: io_data[2] = v; break;
    case 0x09: io_ctrl[0] = v; break;
    case 0x0B: io_ctrl[1] = v; break;
    case 0x0D: io_ctrl[2] = v; break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// 68000 bus
// ---------------------------------------------------------------------------
uint8_t MegaDrive::read8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < 0x400000) return rom[a % rom.size()];
    if (a >= 0xE00000) return ram[a & 0xFFFF];
    if (a >= 0xA00000 && a < 0xA10000) {
        uint32_t z = a & 0x7FFF;
        if (z < 0x4000) return zram[z & 0x1FFF];
        if (z < 0x6000) return ym_read(now_68k());
        return 0xFF;
    }
    if (a >= 0xA10000 && a < 0xA10020) return io_read(a);
    if ((a & 0xFFFF00) == 0xA11100) return (a & 1) ? 0 : (uint8_t)(z80_busreq ? 0 : 1);
    if ((a & 0xE70000) == 0xC00000) {
        vdp.mclk_now = now_68k();
        switch (a & 0x1F) {
        case 0x00: case 0x01: case 0x02: case 0x03: { uint16_t v = vdp.read_data(); return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8); }
        case 0x04: case 0x05: case 0x06: case 0x07: { uint16_t v = vdp.read_status(); return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8); }
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: {
            uint16_t v = vdp.read_hv(); return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8);
        }
        default: return 0xFF;
        }
    }
    return 0xFF;
}

uint16_t MegaDrive::read16(uint32_t a) {
    a &= 0xFFFFFE;
    if (a < 0x400000) return rom16(a);
    if (a >= 0xE00000) return ram16(a);
    if (a >= 0xA00000 && a < 0xA10000) { uint8_t b = read8(a); return (uint16_t)((b << 8) | b); }
    if (a >= 0xA10000 && a < 0xA10020) { uint8_t b = io_read(a | 1); return (uint16_t)((b << 8) | b); }
    if ((a & 0xFFFF00) == 0xA11100) return (uint16_t)(z80_busreq ? 0x0000 : 0x0100);
    if ((a & 0xE70000) == 0xC00000) {
        vdp.mclk_now = now_68k();
        switch (a & 0x1F) {
        case 0x00: case 0x02: return vdp.read_data();
        case 0x04: case 0x06: return vdp.read_status();
        case 0x08: case 0x0A: case 0x0C: case 0x0E: return vdp.read_hv();
        default: return 0xFFFF;
        }
    }
    return 0xFFFF;
}

void MegaDrive::write8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) {
        ram[a & 0xFFFF] = v;
        if (ram_watch && on_ram_write) on_ram_write(*this, a, v, 1);
        return;
    }
    if (a < 0x400000) return;
    if (a >= 0xA00000 && a < 0xA10000) {
        uint32_t z = a & 0x7FFF;
        if (z < 0x4000) zram[z & 0x1FFF] = v;
        else if (z < 0x6000) ym_write(z & 3, v, now_68k());
        else if (z < 0x6100) z80_bank = ((z80_bank >> 1) | ((uint32_t)(v & 1) << 8)) & 0x1FF;
        else if (z >= 0x7F11 && z < 0x7F18 && (z & 1)) { sync_audio(now_68k()); psg.write(v); }
        return;
    }
    if (a >= 0xA10000 && a < 0xA10020) { io_write(a, v); return; }
    if (a == 0xA11100) { z80_busreq = (v & 1) != 0; return; }
    if (a == 0xA11200) {
        bool rst = !(v & 1);
        if (rst && !z80_reset) { z80.reset(); sync_audio(now_68k()); ym.reset(); }
        z80_reset = rst;
        return;
    }
    if ((a & 0xE70000) == 0xC00000) {
        vdp.mclk_now = now_68k();
        switch (a & 0x1F) {
        case 0x00: case 0x01: case 0x02: case 0x03: vdp.write_data((uint16_t)((v << 8) | v)); break;
        case 0x04: case 0x05: case 0x06: case 0x07: vdp.write_ctrl((uint16_t)((v << 8) | v)); break;
        case 0x11: case 0x13: case 0x15: case 0x17: sync_audio(now_68k()); psg.write(v); break;
        default: break;
        }
    }
}

void MegaDrive::write16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (a >= 0xE00000) {
        ram[a & 0xFFFF] = (uint8_t)(v >> 8);
        ram[(a + 1) & 0xFFFF] = (uint8_t)v;
        if (ram_watch && on_ram_write) on_ram_write(*this, a, v, 2);
        return;
    }
    if (a < 0x400000) return;
    if (a >= 0xA00000 && a < 0xA10000) { write8(a, (uint8_t)(v >> 8)); return; }
    if (a >= 0xA10000 && a < 0xA10020) { io_write(a | 1, (uint8_t)v); return; }
    if (a == 0xA11100) { z80_busreq = (v & 0x100) != 0; return; }
    if (a == 0xA11200) { write8(a, (uint8_t)(v >> 8)); return; }
    if ((a & 0xE70000) == 0xC00000) {
        vdp.mclk_now = now_68k();
        switch (a & 0x1F) {
        case 0x00: case 0x02: vdp.write_data(v); break;
        case 0x04: case 0x06: vdp.write_ctrl(v); break;
        case 0x10: case 0x12: case 0x14: case 0x16: sync_audio(now_68k()); psg.write((uint8_t)v); break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Z80 bus
// ---------------------------------------------------------------------------
uint8_t MegaDrive::z80_read(uint16_t a) {
    if (a < 0x4000) return zram[a & 0x1FFF];
    if (a < 0x6000) return ym_read(now_z80());
    if (a >= 0x8000) {
        z80.wait_cycles += 3;
        return read8((z80_bank << 15) | (a & 0x7FFF));
    }
    return 0xFF;
}

void MegaDrive::z80_write(uint16_t a, uint8_t v) {
    if (a < 0x4000) { zram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { ym_write(a & 3, v, now_z80()); return; }
    if (a < 0x6100) { z80_bank = ((z80_bank >> 1) | ((uint32_t)(v & 1) << 8)) & 0x1FF; return; }
    if (a >= 0x7F00 && a < 0x7F20) {
        if (a >= 0x7F10 && a < 0x7F18 && (a & 1)) { sync_audio(now_z80()); psg.write(v); }
        return;
    }
    if (a >= 0x8000) {
        z80.wait_cycles += 3;
        write8((z80_bank << 15) | (a & 0x7FFF), v);
    }
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
namespace {
struct SysState {
    uint32_t magic, version;
    uint8_t  z80_busreq, z80_reset, pal, pad0;
    uint32_t z80_bank;
    uint8_t  io_data[3], io_ctrl[3], pad1[2];
    int64_t  m68_clock, z80_clock, audio_clock;
    int32_t  z80_int_clear_line;
    uint64_t frame_count;
    uint32_t module_state[8];   // game-module bookkeeping that belongs to this game session
};
const uint32_t kMagic = 0x53444D43;  // "CMDS"
const uint32_t kVersion = 2;
template <class T> void put(std::vector<uint8_t>& v, const T& t) {
    size_t o = v.size(); v.resize(o + sizeof(T)); memcpy(&v[o], &t, sizeof(T));
}
}

std::vector<uint8_t> MegaDrive::save_state() {
    std::vector<uint8_t> out;
    SysState s;
    memset(&s, 0, sizeof(s));
    s.magic = kMagic; s.version = kVersion;
    s.z80_busreq = z80_busreq; s.z80_reset = z80_reset; s.pal = pal;
    s.z80_bank = z80_bank;
    memcpy(s.io_data, io_data, 3); memcpy(s.io_ctrl, io_ctrl, 3);
    s.m68_clock = m68_clock; s.z80_clock = z80_clock; s.audio_clock = audio_clock;
    s.z80_int_clear_line = z80_int_clear_line;
    s.frame_count = frame_count;
    memcpy(s.module_state, module_state, sizeof(module_state));
    put(out, s);
    auto blob = [&](size_t n, auto&& saver) {
        size_t o = out.size();
        out.resize(o + n);
        saver(&out[o]);
    };
    blob(cpu.state_size(), [&](uint8_t* p) { cpu.save_state(p); });
    blob(z80.state_size(), [&](uint8_t* p) { z80.save_state(p); });
    blob(vdp.state_size(), [&](uint8_t* p) { vdp.save_state(p); });
    blob(ym.state_size(), [&](uint8_t* p) { ym.save_state(p); });
    blob(psg.state_size(), [&](uint8_t* p) { psg.save_state(p); });
    blob(sizeof(ram), [&](uint8_t* p) { memcpy(p, ram, sizeof(ram)); });
    blob(sizeof(zram), [&](uint8_t* p) { memcpy(p, zram, sizeof(zram)); });
    return out;
}

bool MegaDrive::load_state(const std::vector<uint8_t>& data) {
    size_t need = sizeof(SysState) + cpu.state_size() + z80.state_size() + vdp.state_size() +
                  ym.state_size() + psg.state_size() + sizeof(ram) + sizeof(zram);
    if (data.size() != need) return false;
    SysState s;
    memcpy(&s, data.data(), sizeof(s));
    if (s.magic != kMagic || s.version != kVersion) return false;
    const uint8_t* p = data.data() + sizeof(s);
    z80_busreq = s.z80_busreq != 0; z80_reset = s.z80_reset != 0; pal = s.pal != 0;
    z80_bank = s.z80_bank;
    memcpy(io_data, s.io_data, 3); memcpy(io_ctrl, s.io_ctrl, 3);
    m68_clock = s.m68_clock; z80_clock = s.z80_clock; audio_clock = s.audio_clock;
    z80_int_clear_line = s.z80_int_clear_line;
    frame_count = s.frame_count;
    memcpy(module_state, s.module_state, sizeof(module_state));
    cpu.load_state(p); p += cpu.state_size();
    z80.load_state(p); p += z80.state_size();
    vdp.load_state(p); p += vdp.state_size();
    ym.load_state(p); p += ym.state_size();
    psg.load_state(p); p += psg.state_size();
    memcpy(ram, p, sizeof(ram)); p += sizeof(ram);
    memcpy(zram, p, sizeof(zram));
    return true;
}
