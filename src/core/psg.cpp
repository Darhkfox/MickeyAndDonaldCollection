// SN76489 (Sega VDP variant): 16-bit LFSR, white noise taps bits 0 and 3,
// tone period 0 acts as 0x400.
#include "psg.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace {

struct VolTable {
    int v[16];
    VolTable() {
        for (int i = 0; i < 15; i++) v[i] = (int)std::lround(2600.0 * std::pow(10.0, -0.1 * i));  // 2 dB/step
        v[15] = 0;
    }
};
const VolTable VOL;

inline int eff_period(uint16_t p) { return p ? p : 0x400; }

} // namespace

PSG::PSG() { reset(); }

void PSG::reset() {
    for (auto& t : tone) t = 0;
    for (auto& v : vol) v = 15;
    noise_ctrl = 0;
    latch = 0;
    for (int i = 0; i < 4; i++) { counter[i] = 0; out_state[i] = 1; }
    lfsr = 0x8000;
    phase_acc = 0.0;
}

void PSG::write(uint8_t v) {
    int d;
    if (v & 0x80) { latch = (v >> 4) & 7; d = v & 0x0F; }
    else          { d = v & 0x3F; }
    int n = latch >> 1;
    if (latch & 1) {
        vol[n] = (uint8_t)(d & 0x0F);
    } else if (n < 3) {
        // latch byte sets the low 4 bits, data byte the high 6 bits
        tone[n] = (v & 0x80) ? (uint16_t)((tone[n] & 0x3F0) | d) : (uint16_t)((tone[n] & 0x0F) | (d << 4));
    } else {
        noise_ctrl = (uint8_t)(d & 7);
        lfsr = 0x8000;
    }
}

void PSG::generate(int32_t* out, int n, double clock, double out_rate) {
    if (n <= 0) return;
    for (int c = 0; c < 4; c++) channel_level[c] = 0;
    const double ticks_per_sample = clock / 16.0 / out_rate;

    auto shift_noise = [this] {
        int fb = (noise_ctrl & 4) ? ((lfsr ^ (lfsr >> 3)) & 1) : (lfsr & 1);
        lfsr = (uint16_t)((lfsr >> 1) | (fb << 15));
    };
    auto tick = [&] {
        for (int i = 0; i < 3; i++) {
            if (--counter[i] <= 0) {
                counter[i] = eff_period(tone[i]);
                out_state[i] = -out_state[i];
                if (i == 2 && (noise_ctrl & 3) == 3 && out_state[2] > 0) shift_noise();
            }
        }
        // Noise divider toggles a flip-flop; the LFSR shifts on its rising edge.
        if ((noise_ctrl & 3) != 3 && --counter[3] <= 0) {
            counter[3] = 0x10 << (noise_ctrl & 3);
            out_state[3] = -out_state[3];
            if (out_state[3] > 0) shift_noise();
        }
    };

    for (int s = 0; s < n; s++) {
        double acc[4] = {0, 0, 0, 0};
        double rem = ticks_per_sample;
        for (;;) {
            double t = 1.0 - phase_acc;
            bool last = t > rem;
            if (last) t = rem;
            acc[0] += t * out_state[0];
            acc[1] += t * out_state[1];
            acc[2] += t * out_state[2];
            acc[3] += (lfsr & 1) ? t : -t;
            if (last) { phase_acc += t; break; }
            rem -= t;
            phase_acc = 0.0;
            tick();
        }
        int sum = 0;
        for (int c = 0; c < 4; c++) {
            int v = (int)std::lround(acc[c] / ticks_per_sample * VOL.v[vol[c]]);
            int a = std::abs(v);
            if (a > channel_level[c]) channel_level[c] = a;
            if (!((channel_mute >> c) & 1)) sum += v;
        }
        out[s] = sum;
    }
}

int PSG::channel_volume(int ch) const { return (ch >= 0 && ch < 4) ? vol[ch] : 15; }

// Tone channels: effective period (frequency = clock / (32 * period)).
// Noise: equivalent period of the LFSR shift rate, same formula.
int PSG::channel_period(int ch) const {
    if (ch >= 0 && ch < 3) return eff_period(tone[ch]);
    if (ch == 3) return (noise_ctrl & 3) == 3 ? eff_period(tone[2]) : 0x10 << (noise_ctrl & 3);
    return 0;
}

size_t PSG::state_size() const {
    return sizeof tone + sizeof vol + sizeof noise_ctrl + sizeof latch + sizeof counter +
           sizeof out_state + sizeof lfsr + sizeof phase_acc;
}

void PSG::save_state(uint8_t* dst) const {
    auto put = [&dst](const auto& v) { std::memcpy(dst, &v, sizeof v); dst += sizeof v; };
    put(tone); put(vol); put(noise_ctrl); put(latch);
    put(counter); put(out_state); put(lfsr); put(phase_acc);
}

void PSG::load_state(const uint8_t* src) {
    auto get = [&src](auto& v) { std::memcpy(&v, src, sizeof v); src += sizeof v; };
    get(tone); get(vol); get(noise_ctrl); get(latch);
    get(counter); get(out_state); get(lfsr); get(phase_acc);
}
