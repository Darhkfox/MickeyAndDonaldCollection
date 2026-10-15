// Texas Instruments SN76489 (Sega variant) programmable sound generator.
#pragma once
#include <cstdint>
#include <cstddef>

class PSG {
public:
    PSG();
    void reset();
    void write(uint8_t v);

    // Render `n` mono samples at `out_rate` Hz, given the PSG input clock
    // (NTSC: 3579545 Hz). The chip divides by 16 internally; the output is
    // band-limited by averaging the internal ticks falling into each sample.
    // Each channel at full volume contributes about +/-2600.
    void generate(int32_t* out_mono, int n, double clock, double out_rate);

    uint8_t channel_mute = 0;       // bit n mutes channel n (0-2 tone, 3 noise)
    int  channel_level[4] = {};     // peak output of last generate(), for VU meters
    int  channel_volume(int ch) const; // 0..15 attenuation register value (15 = silent)
    int  channel_period(int ch) const;

    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

private:
    uint16_t tone[3] = {};
    uint8_t  vol[4] = {15, 15, 15, 15};
    uint8_t  noise_ctrl = 0;
    uint8_t  latch = 0;
    int      counter[4] = {};
    int      out_state[4] = {1, 1, 1, 1};
    uint16_t lfsr = 0x8000;
    double   phase_acc = 0.0;
};
