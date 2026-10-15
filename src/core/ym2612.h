// Yamaha YM2612 (OPN2) FM synthesis chip.
// Runs at its native output rate: master clock / 7 / 144 (~53267 Hz NTSC).
#pragma once
#include <cstdint>
#include <cstddef>

class YM2612 {
public:
    YM2612();
    void reset();

    // port 0: address (part I, ch 1-3 + globals), 1: data part I,
    // port 2: address (part II, ch 4-6),          3: data part II.
    void    write(int port, uint8_t v);
    // Status register: bit7 busy, bit1 timer B overflow, bit0 timer A overflow.
    uint8_t read_status();

    // Render `n` stereo frames (interleaved L,R) at the chip's native rate.
    // Also advances timers A/B and the busy counter by `n` samples.
    // Output is the sum of the 6 channels; each channel contributes at most
    // about +/-8192 (9-bit DAC value << 5), so the sum fits in int32.
    void generate(int32_t* out_stereo, int n);

    // Options
    bool ladder_effect = true;      // emulate YM2612 DAC "ladder" distortion (Model 1)
    uint8_t channel_mute = 0;       // bit n mutes channel n (0-5), for the music player

    // Per-channel peak level of the last generate() call (0..8192), for VU meters.
    int  channel_level[6] = {};
    // Key-on state per channel (bit per operator), for visualisers.
    uint8_t key_state(int ch) const;
    // Current block/fnum derived frequency in Hz of a channel (0 if silent) - visualiser only.
    float channel_freq(int ch) const;

    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

private:
    struct Impl;
    Impl* impl;
public:
    ~YM2612();
    YM2612(const YM2612&) = delete;
    YM2612& operator=(const YM2612&) = delete;
};
