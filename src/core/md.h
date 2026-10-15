// Mega Drive / Genesis system: memory map, I/O, Z80 bus arbitration, frame scheduling.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <unordered_map>
#include "m68k.h"
#include "z80.h"
#include "vdp.h"
#include "ym2612.h"
#include "psg.h"

// 3-button pad bits (1 = pressed). Bit order matches the TH=1 read-out.
enum MdButton : uint16_t {
    MD_UP = 0x01, MD_DOWN = 0x02, MD_LEFT = 0x04, MD_RIGHT = 0x08,
    MD_B = 0x10, MD_C = 0x20, MD_A = 0x40, MD_START = 0x80,
};

class MegaDrive {
public:
    // NTSC timing
    static constexpr int kMclkPerLine = 3420;
    static constexpr int kLinesNtsc = 262;
    static constexpr int kLinesPal = 313;
    static constexpr double kMclkNtsc = 53693175.0;
    static constexpr double kMclkPal = 53203424.0;
    static constexpr int kMclkPerSample = 7 * 144;       // YM2612 output sample period

    MegaDrive();
    ~MegaDrive();

    bool load_rom(const std::vector<uint8_t>& data);
    void power_on();
    void soft_reset();
    void run_frame();

    void set_pad(int port, uint16_t buttons) { pad_buttons[port & 1] = buttons; }

    double sample_rate() const { return (pal ? kMclkPal : kMclkNtsc) / kMclkPerSample; }
    double frame_rate() const { return (pal ? kMclkPal : kMclkNtsc) / (kMclkPerLine * (double)lines_per_frame()); }
    int  lines_per_frame() const { return pal ? kLinesPal : kLinesNtsc; }

    // Audio of the last frame, interleaved stereo int32 at sample_rate() (unclamped mix).
    std::vector<int32_t> audio;
    int   audio_frames = 0;
    float fm_gain = 1.0f, psg_gain = 1.0f;

    // Components (public for tools / game modules)
    M68k   cpu;
    Z80    z80;
    VDP    vdp;
    YM2612 ym;
    PSG    psg;
    uint8_t ram[0x10000];
    uint8_t zram[0x2000];
    std::vector<uint8_t> rom;
    bool pal = false;
    bool overseas = true;
    uint64_t frame_count = 0;
    uint32_t module_state[8] = {};   // scratch for game modules: cleared on power-on, kept in save states

    // Z80 bus control
    bool z80_busreq = false;
    bool z80_reset = true;
    uint32_t z80_bank = 0;

    // Controller ports
    uint16_t pad_buttons[2] = {0, 0};
    uint8_t  io_data[3] = {0x7F, 0x7F, 0x7F};
    uint8_t  io_ctrl[3] = {0, 0, 0};

    // ---- hooks for game modules and tools ------------------------------------
    // PC hooks: called before the instruction at `addr` executes.
    void add_pc_hook(uint32_t addr, std::function<void(MegaDrive&)> fn);
    void clear_pc_hooks();
    // RAM write watch (68000 side): called after every write to main RAM when set.
    std::function<void(MegaDrive&, uint32_t addr, uint32_t value, int size)> on_ram_write;
    void enable_ram_watch(bool on);
    // Called at the start of each frame / at VBlank (line 224) / end of frame.
    std::function<void(MegaDrive&)> on_frame_start, on_vblank, on_frame_end;

    // RAM helpers (big-endian, address = 68000 address or offset into RAM)
    uint8_t  ram8(uint32_t a) const { return ram[a & 0xFFFF]; }
    uint16_t ram16(uint32_t a) const { a &= 0xFFFF; return (uint16_t)((ram[a] << 8) | ram[(a + 1) & 0xFFFF]); }
    uint32_t ram32(uint32_t a) const { return ((uint32_t)ram16(a) << 16) | ram16(a + 2); }
    void set_ram8(uint32_t a, uint8_t v) { ram[a & 0xFFFF] = v; }
    void set_ram16(uint32_t a, uint16_t v) { a &= 0xFFFF; ram[a] = (uint8_t)(v >> 8); ram[(a + 1) & 0xFFFF] = (uint8_t)v; }
    void set_ram32(uint32_t a, uint32_t v) { set_ram16(a, (uint16_t)(v >> 16)); set_ram16(a + 2, (uint16_t)v); }
    uint16_t rom16(uint32_t a) const { a %= (uint32_t)rom.size(); return (uint16_t)((rom[a] << 8) | rom[a + 1]); }
    uint32_t rom32(uint32_t a) const { return ((uint32_t)rom16(a) << 16) | rom16(a + 2); }
    // ROM patching (applied to the working copy; original kept for reset)
    void patch_rom16(uint32_t a, uint16_t v);
    void restore_rom();
    uint16_t rom_orig16(uint32_t a) const { a %= (uint32_t)rom_orig.size(); return (uint16_t)((rom_orig[a] << 8) | rom_orig[a + 1]); }

    // Save states
    std::vector<uint8_t> save_state();
    bool load_state(const std::vector<uint8_t>& data);

    // ---- bus (used by CPU callbacks) -------------------------------------------
    uint8_t  read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void     write8(uint32_t a, uint8_t v);
    void     write16(uint32_t a, uint16_t v);
    uint8_t  z80_read(uint16_t a);
    void     z80_write(uint16_t a, uint8_t v);

    // current master-clock position within the frame
    int64_t now_68k() const { return m68_clock + (int64_t)cpu.executed() * 7; }
    int64_t now_z80() const { return z80_clock + (int64_t)z80.executed() * 15; }
    int     line_clock() const;

private:
    std::vector<uint8_t> rom_orig;
    int64_t m68_clock = 0, z80_clock = 0, audio_clock = 0;
    int64_t line_start = 0;
    bool    in_z80 = false;
    int     z80_int_clear_line = -1;
    std::unordered_map<uint32_t, std::function<void(MegaDrive&)>> pc_hooks;
    bool ram_watch = false;
    std::vector<int32_t> psg_tmp;
    std::vector<int32_t> ym_tmp;

    void map_pages();
    void run_until(int64_t mclk);
    void sync_audio(int64_t mclk);
    uint8_t io_read(uint32_t a);
    void io_write(uint32_t a, uint8_t v);
    uint8_t pad_read(int port);
    void ym_write(int port, uint8_t v, int64_t t);
    uint8_t ym_read(int64_t t);
    static void hook_dispatch(void* ctx, uint32_t pc);
};
