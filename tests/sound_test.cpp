// Verification program for the YM2612 and PSG cores.
// Drives both chips with register writes, writes WAVs to tests\out\ and prints
// measured values against the expected hardware behaviour.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/core/ym2612.h"
#include "../src/core/psg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;
static const double YM_CLOCK = 7670453.0, FS = YM_CLOCK / 144.0, PSG_CLOCK = 3579545.0;
static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("  [%s] ", ok ? "PASS" : "FAIL");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
    (ok ? g_pass : g_fail)++;
}

static void write_wav(const char* name, const std::vector<int32_t>& s, int channels, double rate, double gain) {
    std::string path = std::string("tests/out/") + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  cannot write %s\n", path.c_str()); return; }
    uint32_t sr = (uint32_t)std::lround(rate), bytes = (uint32_t)(s.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16((uint16_t)channels); u32(sr); u32(sr * channels * 2); u16((uint16_t)(channels * 2)); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (int32_t v : s) {
        double x = std::max(-32768.0, std::min(32767.0, v * gain));
        u16((uint16_t)(int16_t)std::lround(x));
    }
    std::fclose(f);
    std::printf("  wrote %s (%u bytes)\n", path.c_str(), 44 + bytes);
}
// YM test sounds are centred: store the left channel as mono at the native rate.
static void write_ym(const char* name, const std::vector<int32_t>& st) {
    std::vector<int32_t> m;
    for (size_t i = 0; i < st.size(); i += 2) m.push_back(st[i]);
    write_wav(name, m, 1, FS, 2.0);
}

// ---------------------------------------------------------------- analysis
static std::vector<double> chan(const std::vector<int32_t>& st, int c, size_t skip = 0) {
    std::vector<double> v;
    for (size_t i = skip * 2 + c; i < st.size(); i += 2) v.push_back(st[i]);
    return v;
}
static std::vector<double> to_d(const std::vector<int32_t>& m, size_t skip = 0) {
    return std::vector<double>(m.begin() + skip, m.end());
}
static void remove_dc(std::vector<double>& x) {
    double m = 0;
    for (double v : x) m += v;
    m /= x.size();
    for (double& v : x) v -= m;
}
// Frequency from interpolated positive-going zero crossings.
static double measure_freq(std::vector<double> x, double fs) {
    remove_dc(x);
    double first = -1, last = -1;
    int count = 0;
    for (size_t i = 1; i < x.size(); i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = (i - 1) + (-x[i - 1]) / (x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    return count > 1 ? (count - 1) / ((last - first) / fs) : 0.0;
}
// Hann-windowed single-bin DFT amplitude at frequency f.
static double tone_amp(const std::vector<double>& x, double f, double fs) {
    double re = 0, im = 0, w = 2 * PI * f / fs;
    size_t n = x.size();
    for (size_t i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * std::cos(2 * PI * i / (n - 1));
        re += x[i] * win * std::cos(w * i);
        im -= x[i] * win * std::sin(w * i);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (n * 0.5);
}
static double db(double r) { return 20.0 * std::log10(std::max(r, 1e-12)); }
// Harmonic levels 2..nh relative to the fundamental (dB).
static std::vector<double> harmonics(const std::vector<double>& x, double f0, double fs, int nh) {
    std::vector<double> h;
    double a1 = tone_amp(x, f0, fs);
    h.push_back(a1);
    for (int k = 2; k <= nh; k++) h.push_back(k * f0 < fs / 2 ? db(tone_amp(x, k * f0, fs) / a1) : -200);
    return h;
}

// ---------------------------------------------------------------- YM helpers
struct FM {
    YM2612 ym;
    FM() { ym.ladder_effect = false; }
    void w(int part, int reg, int val) { ym.write(part * 2, (uint8_t)reg); ym.write(part * 2 + 1, (uint8_t)val); }
    void wch(int ch, int reg, int val) { w(ch / 3, reg + ch % 3, val); }
    void wop(int ch, int op, int reg, int val) {
        static const int off[5] = {0, 0, 8, 4, 12};   // op1, op2 (+8), op3 (+4), op4 (+12)
        w(ch / 3, reg + off[op] + ch % 3, val);
    }
    void freq(int ch, int block, int fnum) { wch(ch, 0xA4, (block << 3) | (fnum >> 8)); wch(ch, 0xA0, fnum & 0xFF); }
    void key(int ch, int opmask) { w(0, 0x28, (opmask << 4) | (ch < 3 ? ch : ch + 1)); }
    void patch(int ch, int op, int dtmul, int tl, int ar = 31, int d1r = 0, int d2r = 0, int sl = 0,
               int rr = 15, int ks = 0, int am = 0, int ssg = 0) {
        wop(ch, op, 0x30, dtmul); wop(ch, op, 0x40, tl); wop(ch, op, 0x50, (ks << 6) | ar);
        wop(ch, op, 0x60, (am << 7) | d1r); wop(ch, op, 0x70, d2r); wop(ch, op, 0x80, (sl << 4) | rr);
        wop(ch, op, 0x90, ssg);
    }
    void algfb(int ch, int alg, int fb) { wch(ch, 0xB0, (fb << 3) | alg); }
    std::vector<int32_t> run(int n) {
        std::vector<int32_t> v((size_t)n * 2);
        ym.generate(v.data(), n);
        return v;
    }
};
static double ym_hz(int fnum, int block, int mul2 = 2, int dt = 0) {   // mul2: MUL*2, or 1 for MUL 0
    double base = (double)(((fnum << block) >> 1) + dt);
    return std::floor(base * mul2 / 2) * FS / 1048576.0;
}
// Single sine on ch0: alg 7, op4 only.
static void sine_setup(FM& fm, int block, int fnum, int dtmul = 1) {
    fm.algfb(0, 7, 0);
    fm.patch(0, 4, dtmul, 0);
    fm.freq(0, block, fnum);
    fm.key(0, 8);
}

// ---------------------------------------------------------------- YM tests
static void test_pitch() {
    std::printf("\n== YM2612 pitch / sine shape ==\n");
    {
        FM fm;
        sine_setup(fm, 4, 617);
        auto st = fm.run((int)FS);
        auto x = chan(st, 0, 100);
        double f = measure_freq(x, FS), e = ym_hz(617, 4);
        check(std::fabs(f / e - 1) < 1e-4, "fnum 617 block 4: %.4f Hz measured, %.4f Hz expected", f, e);
        auto h = harmonics(x, e, FS, 6);
        double worst = *std::max_element(h.begin() + 1, h.end());
        check(worst < -40, "sine purity: fundamental amp %.0f, H2..H6 = %.1f %.1f %.1f %.1f %.1f dB", h[0],
              h[1], h[2], h[3], h[4], h[5]);
        double pk = 0;
        for (double v : x) pk = std::max(pk, std::fabs(v));
        check(pk >= 8100 && pk <= 8192, "peak level %.0f (9-bit DAC range +8160 / -8192)", pk);
        write_ym("ym_sine_250hz.wav", std::vector<int32_t>(st.begin(), st.begin() + (size_t)FS));
    }
    for (int block = 0; block < 8; block++) {
        FM fm;
        int fnum = 1000 + block * 97;
        sine_setup(fm, block, fnum);
        auto x = chan(fm.run((int)FS * (block < 2 ? 2 : 1)), 0, 100);
        double f = measure_freq(x, FS), e = fnum * std::ldexp(1.0, block - 1) * FS / 1048576.0;
        check(std::fabs(f / e - 1) < 5e-4, "block %d fnum %4d: %9.3f Hz, formula %9.3f Hz", block, fnum, f, e);
    }
}

static void test_mul_dt() {
    std::printf("\n== YM2612 MUL / DT ==\n");
    const int mul2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
    for (int mul : {0, 1, 2, 3, 7, 15}) {
        FM fm;
        sine_setup(fm, 4, 617, mul);
        double f = measure_freq(chan(fm.run((int)FS), 0, 100), FS), e = ym_hz(617, 4, mul2[mul]);
        check(std::fabs(f / e - 1) < 2e-4, "MUL %2d: %8.3f Hz (x%.2f of MUL 1), expected %8.3f", mul, f,
              f / ym_hz(617, 4), e);
    }
    // DT table row for keycode 24 (block 6, fnum 617): 0, 5, 11, 16 (MAME/hardware dt_tab)
    const int dt24[4] = {0, 5, 11, 16};
    for (int dt : {0, 1, 2, 3, 5, 6, 7}) {
        FM fm;
        sine_setup(fm, 6, 617, (dt << 4) | 1);
        int d = (dt & 4) ? -dt24[dt & 3] : dt24[dt & 3];
        double f = measure_freq(chan(fm.run((int)FS * 2), 0, 100), FS), e = ym_hz(617, 6, 2, d);
        check(std::fabs(f / e - 1) < 2e-5, "DT %d: %9.4f Hz (%+.3f%%), expected %9.4f", dt, f,
              (f / ym_hz(617, 6) - 1) * 100, e);
    }
}

// Envelope via a quarter-rate carrier: inc = 0x40000 so every 4th sample hits the sine peak.
static std::vector<double> envelope(FM& fm, int n) {
    auto x = chan(fm.run(n), 0);
    std::vector<double> e;
    for (size_t i = 0; i + 4 <= x.size(); i += 4)
        e.push_back(std::max({std::fabs(x[i]), std::fabs(x[i + 1]), std::fabs(x[i + 2]), std::fabs(x[i + 3])}));
    return e;
}
static const uint8_t EG_INC[16][8] = {
    {0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1}, {1,1,1,2,1,1,1,2}, {1,2,1,2,1,2,1,2}, {1,2,2,2,1,2,2,2},
    {2,2,2,2,2,2,2,2}, {2,2,2,4,2,2,2,4}, {2,4,2,4,2,4,2,4}, {2,4,4,4,2,4,4,4},
    {4,4,4,4,4,4,4,4}, {4,4,4,8,4,4,4,8}, {4,8,4,8,4,8,4,8}, {4,8,8,8,4,8,8,8}};
static int eg_row(int r) { return r < 48 ? (r & 3) : r < 60 ? 4 + (r - 48) : 15; }
static int eg_shift(int r) { return r < 48 ? 11 - (r >> 2) : 0; }
static double eg_avg(int r) {
    double s = 0;
    for (int i = 0; i < 8; i++) s += EG_INC[r >= 60 ? 12 : eg_row(r)][i];
    return (r >= 60 ? 8.0 : s / 8);   // rates 60-63: +8 per step
}
// Expected sample counts from the documented OPN EG rules (EG clock = fs/3).
static double decay_samples(int rate, int att) { return att / eg_avg(rate) * (1 << eg_shift(rate)) * 3; }
static double attack_samples(int rate) {
    int v = 1023, steps = 0, k = 0;
    while (v > 0) {
        int inc = EG_INC[eg_row(rate)][k++ & 7];
        v += (~v * inc) >> 4;
        steps++;
    }
    return (double)steps * (1 << eg_shift(rate)) * 3;
}

static void test_envelope() {
    std::printf("\n== YM2612 envelope timing (fs = %.2f Hz, EG clock fs/3) ==\n", FS);
    // Carrier at fnum 1024 block 7 MUL 4 -> keycode 30, KS 0 adds 30 >> 3 = 3 to the rate.
    for (int ar : {12, 16, 20, 24, 28, 31}) {
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, ar);
        fm.freq(0, 7, 1024);
        fm.key(0, 8);
        auto e = envelope(fm, (int)FS * 2);
        size_t t = 0;
        while (t < e.size() && e[t] < 8160) t++;
        int rate = std::min(63, 2 * ar + 3);
        double exp_s = rate >= 62 ? 0 : attack_samples(rate), got = t * 4.0;
        double tol = (1 << eg_shift(rate)) * 3 * 8 + 8 + exp_s * 0.05;
        check(std::fabs(got - exp_s) <= tol, "attack AR %2d (rate %2d): %8.2f ms to 0 dB, expected %8.2f ms", ar,
              rate, got / FS * 1000, exp_s / FS * 1000);
    }
    for (int d1r : {12, 16, 20, 24, 29}) {
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, d1r, 0, 15);
        fm.freq(0, 7, 1024);
        fm.key(0, 8);
        auto e = envelope(fm, (int)FS * 3);
        size_t t = 0;
        while (t < e.size() && e[t] > 480) t++;   // 24.1 dB = 256 attenuation units
        int rate = std::min(63, 2 * d1r + 3);
        double exp_s = decay_samples(rate, 256), got = t * 4.0;
        double tol = (1 << eg_shift(rate)) * 3 * 8 + 8 + exp_s * 0.03;
        check(std::fabs(got - exp_s) <= tol,
              "decay D1R %2d (rate %2d): %8.2f ms to -24 dB, expected %8.2f ms (0..96 dB: %.1f ms)", d1r, rate,
              got / FS * 1000, exp_s / FS * 1000, decay_samples(rate, 1024) / FS * 1000);
    }
    {   // release: RR 8 -> rate 2*17+3 = 37
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, 0, 0, 0, 8);
        fm.freq(0, 7, 1024);
        fm.key(0, 8);
        fm.run(1000);
        fm.key(0, 0);
        auto e = envelope(fm, (int)FS);
        size_t t = 0;
        while (t < e.size() && e[t] > 480) t++;
        double exp_s = decay_samples(37, 256), got = t * 4.0;
        check(std::fabs(got - exp_s) <= (1 << eg_shift(37)) * 24 + 8 + exp_s * 0.03,
              "release RR 8 (rate 37): %.2f ms to -24 dB, expected %.2f ms", got / FS * 1000, exp_s / FS * 1000);
    }
    {   // audible ADSR example + key scaling: KS 3 at keycode 30 adds 30 to the rate
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 1, 0, 14, 8, 3, 4, 6);
        fm.freq(0, 4, 617);
        fm.key(0, 8);
        auto a = fm.run((int)(FS * 0.6));
        fm.key(0, 0);
        auto b = fm.run((int)(FS * 0.4));
        a.insert(a.end(), b.begin(), b.end());
        write_ym("ym_envelope_adsr.wav", a);

        FM k0, k3;
        for (FM* f : {&k0, &k3}) {
            f->algfb(0, 7, 0);
            f->patch(0, 4, 4, 0, 31, 12, 0, 15, 15, f == &k3 ? 3 : 0);
            f->freq(0, 7, 1024);
            f->key(0, 8);
        }
        auto e0 = envelope(k0, (int)FS), e3 = envelope(k3, (int)FS);
        size_t t0 = 0, t3 = 0;
        while (t0 < e0.size() && e0[t0] > 480) t0++;
        while (t3 < e3.size() && e3[t3] > 480) t3++;
        double x0 = decay_samples(27, 256), x3 = decay_samples(54, 256);
        check(std::fabs(t0 * 4.0 - x0) < x0 * 0.1 && std::fabs(t3 * 4.0 - x3) < x3 * 0.1 + 30,
              "key scale: D1R 12 KS0 rate 27 -> %.2f ms (exp %.2f), KS3 rate 54 -> %.2f ms (exp %.2f)",
              t0 * 4.0 / FS * 1000, x0 / FS * 1000, t3 * 4.0 / FS * 1000, x3 / FS * 1000);
    }
}

static void test_ssg() {
    std::printf("\n== YM2612 SSG-EG shapes (AR 31, D1R 24, SL 15) ==\n");
    // expected: starts high?, ends holding (H)igh / (L)ow or (C)ycling, big jumps up/down
    struct { const char* shape; bool start_high; char end; bool up, down; } ex[8] = {
        {"\\\\\\\\", true, 'C', true, false}, {"\\___", true, 'L', false, true},
        {"\\/\\/", true, 'C', false, false},   {"\\---", true, 'H', true, false},
        {"////", false, 'C', false, true},     {"/---", false, 'H', false, false},
        {"/\\/\\", false, 'C', false, false},  {"/___", false, 'L', false, true}};
    std::vector<int32_t> wav;
    for (int m = 0; m < 8; m++) {
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, 24, 24, 15, 15, 0, 0, 8 + m);
        fm.freq(0, 7, 1024);
        fm.key(0, 8);
        auto e = envelope(fm, 6000);
        bool start_high = e[1] > 4000, up = false, down = false;
        for (size_t i = 1; i < e.size(); i++) {
            if (e[i] - e[i - 1] > 4000) up = true;
            if (e[i - 1] - e[i] > 4000) down = true;
        }
        double mx = *std::max_element(e.end() - 150, e.end()), mn = *std::min_element(e.end() - 150, e.end());
        char end = mx - mn > 1000 ? 'C' : mx > 4000 ? 'H' : mn < 300 ? 'L' : '?';
        // a hold-low after a decay/attack ends with one drop to silence; allow it
        bool ok = start_high == ex[m].start_high && end == ex[m].end && up == ex[m].up &&
                  (down == ex[m].down || ex[m].end == 'L');
        check(ok, "SSG mode %X %-5s: start %s, end %c, jump up %d, jump down %d", 8 + m, ex[m].shape,
              start_high ? "high" : "low ", end, up, down);
        FM a;   // audible version at 250 Hz for the WAV
        a.algfb(0, 7, 0);
        a.patch(0, 4, 1, 0, 31, 18, 18, 15, 15, 0, 0, 8 + m);
        a.freq(0, 4, 617);
        a.key(0, 8);
        auto s = a.run((int)(FS * 0.25));
        wav.insert(wav.end(), s.begin(), s.end());
    }
    write_ym("ym_ssg_modes_8_to_F.wav", wav);
}

static double thd_db(const std::vector<double>& x, double f0) {   // strongest of H2..H6 vs H1
    auto h = harmonics(x, f0, FS, 6);
    return *std::max_element(h.begin() + 1, h.end());
}

static void test_algorithms() {
    std::printf("\n== YM2612 algorithms / feedback ==\n");
    const double f0 = ym_hz(617, 4);
    // Carriers and direct modulation edges (m -> c) for each algorithm.
    const int carriers[8] = {8, 8, 8, 8, 10, 14, 14, 15};   // bit (op-1)
    const char* edges[8] = {"12 23 34", "13 23 34", "14 23 34", "12 24 34", "12 34", "12 13 14", "12", ""};
    for (int alg = 0; alg < 8; alg++) {
        bool ok = true;
        std::string car;
        for (int op = 1; op <= 4; op++) {   // key a single operator: audible only if it is a carrier
            FM fm;
            fm.algfb(0, alg, 0);
            fm.patch(0, op, 1, 0);
            fm.freq(0, 4, 617);
            fm.key(0, 1 << (op - 1));
            auto x = chan(fm.run(4000), 0, 1000);
            double pk = 0;
            for (double v : x) pk = std::max(pk, std::fabs(v));
            bool is_car = (carriers[alg] >> (op - 1)) & 1;
            if (is_car != (pk > 4000)) ok = false;
            if (pk > 4000) car += (char)('0' + op);
        }
        std::string mods;
        for (int c = 1; c <= 4; c++) {
            if (!((carriers[alg] >> (c - 1)) & 1)) continue;
            for (int m = 1; m <= 4; m++) {
                if (m == c) continue;
                FM fm;
                fm.algfb(0, alg, 0);
                fm.patch(0, m, 1, 8);
                fm.patch(0, c, 1, 8);   // two carriers at -6 dB each: sum stays below the 14-bit clip
                fm.freq(0, 4, 617);
                fm.key(0, (1 << (m - 1)) | (1 << (c - 1)));
                double t = thd_db(chan(fm.run(12000), 0, 2000), f0);
                char e[3] = {(char)('0' + m), (char)('0' + c), 0};
                bool edge = std::strstr(edges[alg], e) != nullptr;
                if (edge ? t < -30 : t > -40) ok = false;
                if (t > -30) { mods += e; mods += ' '; }
            }
        }
        check(ok, "alg %d: carriers {%s}, modulated pairs {%s} (expected {%s})", alg, car.c_str(), mods.c_str(),
              edges[alg]);
    }
    // Spectra with all four operators at TL 16.
    double cen[8];
    for (int alg = 0; alg < 8; alg++) {
        FM fm;
        fm.algfb(0, alg, 0);
        for (int op = 1; op <= 4; op++) fm.patch(0, op, 1, 16);
        fm.freq(0, 4, 617);
        fm.key(0, 15);
        auto st = fm.run((int)FS / 2);
        auto x = chan(st, 0, 2000);
        double a[12], tot = 0, c = 0, mx = 0;
        for (int k = 1; k <= 12; k++) {
            a[k - 1] = tone_amp(x, k * f0, FS);
            tot += a[k - 1] * a[k - 1];
            c += k * a[k - 1] * a[k - 1];
            mx = std::max(mx, a[k - 1]);
        }
        cen[alg] = c / tot;
        std::printf("  alg %d harmonics 1-8 (dB):", alg);
        for (int k = 0; k < 8; k++) std::printf(" %6.1f", db(a[k] / mx));
        std::printf("  centroid %.2f\n", cen[alg]);
        if (alg == 0) write_ym("ym_alg0.wav", st);
        if (alg == 7) write_ym("ym_alg7.wav", st);
    }
    check(cen[7] < 1.01 && cen[0] > 2.0, "alg 0 spectrum rich (centroid %.2f) vs alg 7 pure (centroid %.2f)", cen[0],
          cen[7]);

    double prev = -1e9;
    bool mono = true;
    std::vector<int32_t> fbwav;
    for (int fb = 0; fb < 8; fb++) {
        FM fm;
        fm.algfb(0, 7, fb);
        fm.patch(0, 1, 1, 0);
        fm.freq(0, 4, 617);
        fm.key(0, 1);
        auto st = fm.run((int)FS / 5);
        auto x = chan(st, 0, 1000);
        auto h = harmonics(x, f0, FS, 8);
        double e = 0;
        for (int k = 1; k < 7; k++) e += std::pow(10.0, h[k] / 10);
        double thd = 10 * std::log10(e);
        std::printf("  FB %d: H2 %6.1f  H3 %6.1f  H4 %6.1f  THD %6.1f dB\n", fb, h[1], h[2], h[3], thd);
        if (fb == 0 && thd > -40) mono = false;
        if (fb > 0 && fb < 6 && thd < prev) mono = false;   // FB 5-6 saturate near a sawtooth (-3 dB)
        if (fb >= 5 && thd < -6) mono = false;
        prev = thd;
        fbwav.insert(fbwav.end(), st.begin(), st.end());
    }
    check(mono, "feedback: FB 0 is a pure sine, harmonic content rises with FB 1..5, saw-like at FB 5+");
    write_ym("ym_feedback_0to7.wav", fbwav);
}

static void test_dac() {
    std::printf("\n== YM2612 DAC / ladder effect ==\n");
    FM fm;
    fm.w(1, 0xB6, 0x80);   // ch6 left only
    fm.w(0, 0x2B, 0x80);
    bool ok = true;
    for (int k = 0; k < 256; k++) {
        fm.w(0, 0x2A, k);
        auto s = fm.run(1);
        if (s[0] != (k - 128) * 64 || s[1] != 0) ok = false;
    }
    check(ok, "linear DAC: L == (v-128)*64 for all 256 values, R == 0 (hard pan)");
    fm.ym.ladder_effect = true;
    fm.ym.channel_mute = 0x1F;
    ok = true;
    for (int k = 0; k < 256; k++) {
        fm.w(0, 0x2A, k);
        auto s = fm.run(1);
        int l = ((k - 128) * 2 + (k >= 128 ? 4 : -3)) * 32, r = (k >= 128 ? 4 : -4) * 32;
        if (s[0] != l || s[1] != r) ok = false;
    }
    check(ok, "ladder DAC: +4 offset for v>=0, -3 (panned) / -4 (unpanned) for v<0, in 9-bit units");
    fm.w(0, 0x2B, 0x00);
    auto s = fm.run(4);
    check(s[6] == 4 * 32 && fm.ym.channel_level[5] == 0, "DAC disabled -> ch6 back to (silent) FM output");

    FM d;   // 8-bit PCM sine at ~17.8 kHz (one write every 3 samples), ch6 centre
    d.w(0, 0x2B, 0x80);
    std::vector<int32_t> w;
    for (int i = 0; i < (int)FS / 2 / 3; i++) {
        d.w(0, 0x2A, 128 + (int)std::lround(100 * std::sin(2 * PI * 440.0 * i * 3 / FS)));
        auto t = d.run(3);
        w.insert(w.end(), t.begin(), t.end());
    }
    double f = measure_freq(chan(w, 0), FS), pk = 0;
    for (size_t i = 0; i < w.size(); i += 2) pk = std::max(pk, std::fabs((double)w[i]));
    check(std::fabs(f - 440) < 0.5 && std::fabs(pk - 6400) < 100, "DAC stream: %.2f Hz, peak %.0f (100*64 = 6400)",
          f, pk);
    write_ym("ym_dac_440hz.wav", w);

    // Ladder crossover distortion on a quiet note (TL 40 = -30 dB).
    double thd[2];
    for (int lad = 0; lad < 2; lad++) {
        FM q;
        q.ym.ladder_effect = lad != 0;
        q.ym.channel_mute = 0x3E;
        sine_setup(q, 4, 617);
        q.patch(0, 4, 1, 40);
        thd[lad] = thd_db(chan(q.run((int)FS / 2), 0, 1000), ym_hz(617, 4));
    }
    check(thd[1] > thd[0] + 6, "quiet note THD: linear %.1f dB, ladder %.1f dB (crossover distortion audible)", thd[0],
          thd[1]);
}

static std::vector<int> overflow_times(FM& fm, int bit, int n, uint8_t rearm) {
    std::vector<int> t;
    for (int i = 0; i < n; i++) {
        fm.run(1);
        if (fm.ym.read_status() & bit) { t.push_back(i + 1); fm.w(0, 0x27, rearm); }
    }
    return t;
}
static bool all_eq(const std::vector<int>& t, int period, size_t from) {
    if (t.size() < from + 2) return false;
    for (size_t i = from + 1; i < t.size(); i++) if (t[i] - t[i - 1] != period) return false;
    return true;
}

static void test_timers() {
    std::printf("\n== YM2612 timers / busy / CSM ==\n");
    for (int ta : {1000, 0, 1023, 777}) {
        FM fm;
        fm.w(0, 0x24, ta >> 2);
        fm.w(0, 0x25, ta & 3);
        fm.w(0, 0x27, 0x15);
        auto t = overflow_times(fm, 1, 5000, 0x15);
        int p = 1024 - ta;
        check(!t.empty() && t[0] == p && all_eq(t, p, 0), "timer A = %4d: first overflow after %d samples, period %d (exp %d), %zu overflows",
              ta, t.empty() ? -1 : t[0], t.size() > 1 ? t[1] - t[0] : -1, p, t.size());
    }
    {
        FM fm;
        fm.w(0, 0x24, 250);
        fm.w(0, 0x27, 0x01);   // running, flag disabled
        fm.run(200);
        bool none = !(fm.ym.read_status() & 1);
        fm.w(0, 0x27, 0x05);
        fm.run(24);
        bool set = (fm.ym.read_status() & 1) != 0;
        fm.w(0, 0x27, 0x15);
        bool cleared = !(fm.ym.read_status() & 3);
        fm.w(0, 0x27, 0x00);
        fm.run(3000);
        check(none && set && cleared && !(fm.ym.read_status() & 1),
              "timer A flag: masked when enable bit clear, set when enabled, reset by bit 4, stops when unloaded");
    }
    for (int tb : {200, 0, 255}) {
        FM fm;
        fm.w(0, 0x26, tb);
        fm.w(0, 0x27, 0x2A);
        auto t = overflow_times(fm, 2, 20000, 0x2A);
        int p = (256 - tb) * 16;
        check(!t.empty() && t[0] <= p && all_eq(t, p, 0), "timer B = %3d: first overflow %d (<= %d), period %d (exp %d)", tb,
              t.empty() ? -1 : t[0], p, t.size() > 1 ? t[1] - t[0] : -1, p);
    }
    {
        FM fm;
        fm.run(10);
        bool idle = !(fm.ym.read_status() & 0x80);
        fm.w(0, 0x30, 0x01);
        bool b0 = (fm.ym.read_status() & 0x80) != 0;
        fm.run(1);
        bool b1 = (fm.ym.read_status() & 0x80) != 0;
        fm.run(1);
        bool b2 = (fm.ym.read_status() & 0x80) != 0;
        check(idle && b0 && b1 && !b2, "busy flag: set by data write, still set after 1 sample, clear after 2 (32 FM cycles)");
    }
    {
        FM fm;
        fm.algfb(2, 7, 0);
        for (int op = 1; op <= 4; op++) fm.patch(2, op, 1, 10, 31, 31, 0, 15, 15);
        fm.freq(2, 4, 617);
        fm.w(0, 0x24, 250);
        fm.w(0, 0x27, 0x85);   // CSM + load/enable timer A (period 24)
        int on = 0, first = -1, last = -1;
        double pk = 0;
        for (int i = 0; i < 480; i++) {
            auto s = fm.run(1);
            pk = std::max(pk, std::fabs((double)s[0]));
            if (fm.ym.key_state(2) == 0xF) { on++; if (first < 0) first = i; last = i; }
            if (fm.ym.read_status() & 1) fm.w(0, 0x27, 0x95);
        }
        fm.w(0, 0x27, 0x05);
        fm.run(100);
        check(on == 20 && first == 23 && (last - first) == 19 * 24 && pk > 1000 && fm.ym.key_state(2) == 0,
              "CSM: %d one-sample key-ons every 24 samples (first at %d), notes audible (peak %.0f)", on, first, pk);
    }
}

static void test_ch3_special() {
    std::printf("\n== YM2612 channel 3 special mode / F-number latch ==\n");
    struct { int op, reg, blk, fn; } sp[3] = {{1, 0xA9, 5, 500}, {2, 0xAA, 3, 1500}, {3, 0xA8, 6, 300}};
    for (int mode : {0x40, 0x00}) {
        std::string res;
        bool ok = true;
        for (int op = 1; op <= 4; op++) {
            FM fm;
            fm.algfb(2, 7, 0);
            fm.patch(2, op, 1, 0);
            fm.freq(2, 4, 617);
            for (auto& s : sp) { fm.w(0, s.reg + 4, (s.blk << 3) | (s.fn >> 8)); fm.w(0, s.reg, s.fn & 0xFF); }
            fm.w(0, 0x27, mode);
            fm.key(2, 1 << (op - 1));
            double f = measure_freq(chan(fm.run((int)FS / 2), 0, 100), FS);
            double e = (mode && op < 4) ? ym_hz(sp[op - 1].fn, sp[op - 1].blk) : ym_hz(617, 4);
            if (std::fabs(f / e - 1) > 2e-4) ok = false;
            char b[64];
            std::snprintf(b, sizeof b, " op%d %.2f Hz", op, f);
            res += b;
        }
        check(ok, "%s mode:%s", mode ? "special" : "normal ", res.c_str());
    }
    FM fm;
    sine_setup(fm, 4, 617);
    fm.patch(1, 4, 1, 0);
    fm.algfb(1, 7, 0);
    fm.key(1, 8);
    fm.run(10);
    float f1 = fm.ym.channel_freq(0);
    fm.wch(0, 0xA4, (5 << 3) | 2);
    fm.run(10);
    float f2 = fm.ym.channel_freq(0);
    fm.wch(0, 0xA0, 0x10);
    fm.run(10);
    float f3 = fm.ym.channel_freq(0);
    fm.wch(0, 0xA4, (3 << 3) | 1);   // latch written for ch1 ...
    fm.wch(1, 0xA0, 0x23);           // ... is shared: ch2's low byte commits it
    fm.run(10);
    float f4 = fm.ym.channel_freq(1);
    double e3 = 0x210 * 16.0 * FS / 1048576.0, e4 = 0x123 * 4.0 * FS / 1048576.0;
    check(f1 == f2 && std::fabs(f3 / e3 - 1) < 1e-5 && std::fabs(f4 / e4 - 1) < 1e-5,
          "A4 latch: %.2f Hz unchanged (%.2f) until A0 write -> %.2f Hz (exp %.2f); shared latch -> ch2 %.2f Hz (exp %.2f)",
          f1, f2, f3, e3, f4, e4);
}

static void test_lfo() {
    std::printf("\n== YM2612 LFO ==\n");
    const int per[8] = {108, 77, 71, 67, 62, 44, 8, 5};
    const double exp_db[4] = {0, 15 * 6.0206 / 64, 63 * 6.0206 / 64, 126 * 6.0206 / 64};
    for (int ams = 1; ams < 4; ams++) {
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, 0, 0, 0, 15, 0, 1);
        fm.freq(0, 7, 1024);
        fm.wch(0, 0xB4, 0xC0 | (ams << 4));
        fm.w(0, 0x22, 0x08 | 3);
        fm.key(0, 8);
        auto e = envelope(fm, (int)FS * 2);
        e.erase(e.begin(), e.begin() + 10);
        double mx = *std::max_element(e.begin(), e.end()), mn = *std::min_element(e.begin(), e.end());
        check(std::fabs(db(mx / mn) - exp_db[ams]) < 0.2, "AMS %d: AM depth %.2f dB (expected %.2f dB)", ams,
              db(mx / mn), exp_db[ams]);
    }
    std::string rs;
    bool ok = true;
    for (int r = 0; r < 8; r++) {
        FM fm;
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, 0, 0, 0, 15, 0, 1);
        fm.freq(0, 7, 1024);
        fm.wch(0, 0xB4, 0xF0);
        fm.w(0, 0x22, 0x08 | r);
        fm.key(0, 8);
        double f = measure_freq(envelope(fm, (int)FS * 3), FS / 4), x = FS / (128.0 * per[r]);
        if (std::fabs(f / x - 1) > 0.01) ok = false;
        char b[32];
        std::snprintf(b, sizeof b, " %.2f", f);
        rs += b;
    }
    check(ok, "LFO rates 0-7 (Hz):%s (expected fs/(128*{108,77,71,67,62,44,8,5}))", rs.c_str());
    {
        FM fm;   // LFO disabled: AM output is held at its reset value (126 -> -11.8 dB)
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 4, 0, 31, 0, 0, 0, 15, 0, 1);
        fm.freq(0, 7, 1024);
        fm.wch(0, 0xB4, 0xF0);
        fm.key(0, 8);
        auto e = envelope(fm, 20000);
        double mx = *std::max_element(e.begin() + 10, e.end()), mn = *std::min_element(e.begin() + 10, e.end());
        check(mx == mn && std::fabs(db(mx / 8160) + exp_db[3]) < 0.2, "LFO off + AMS 3: constant level %.0f (%.2f dB)", mx,
              db(mx / 8160));
    }
    {
        FM fm;   // PM: PMS 7 at fnum 1024 -> +/-96 half-fnum units = +/-4.69%
        fm.algfb(0, 7, 0);
        fm.patch(0, 4, 1, 0);
        fm.freq(0, 4, 1024);
        fm.wch(0, 0xB4, 0xC7);
        fm.w(0, 0x22, 0x08 | 3);
        fm.key(0, 8);
        auto st = fm.run((int)FS * 2);
        auto x = chan(st, 0);
        std::vector<double> zc;
        for (size_t i = 1; i < x.size(); i++)
            if (x[i - 1] < 0 && x[i] >= 0) zc.push_back((i - 1) + (-x[i - 1]) / (x[i] - x[i - 1]));
        double f0 = ym_hz(1024, 4), hi = 0, lo = 1e9;
        for (size_t i = 2; i < zc.size(); i++) {
            double f = 2 * FS / (zc[i] - zc[i - 2]);
            hi = std::max(hi, f);
            lo = std::min(lo, f);
        }
        check(std::fabs(hi / f0 - 1.046875) < 0.003 && std::fabs(lo / f0 - 0.953125) < 0.003,
              "PMS 7 vibrato: %.2f .. %.2f Hz around %.2f (%+.2f%% / %+.2f%%, expected +/-4.69%%)", lo, hi, f0,
              (hi / f0 - 1) * 100, (lo / f0 - 1) * 100);
        write_ym("ym_lfo_vibrato.wav", std::vector<int32_t>(st.begin(), st.begin() + (size_t)FS));
    }
}

static void song_setup(FM& fm) {
    fm.w(0, 0x22, 0x0A);
    for (int ch = 0; ch < 6; ch++) {
        fm.algfb(ch, (ch * 3) & 7, ch);
        for (int op = 1; op <= 4; op++)
            fm.patch(ch, op, (op * 17 + ch) & 0x7F, 10 + op * 6 + ch, 20 + op + ch, 8 + op, 4, 3 + op, 7, op & 3,
                     op == 2, ch == 4 && op == 1 ? 0x0A : 0);
        fm.wch(ch, 0xB4, 0xC0 | ((ch & 3) << 4) | (ch & 7));
        fm.freq(ch, 3 + (ch & 3), 600 + ch * 50);
        fm.key(ch, 15);
    }
    fm.w(0, 0x24, 200);
    fm.w(0, 0x26, 100);
    fm.w(0, 0x27, 0x3F);
}

static void test_state_api() {
    std::printf("\n== YM2612 state / interleaving / API ==\n");
    {
        FM a;
        song_setup(a);
        a.run(3000);
        std::vector<uint8_t> blob(a.ym.state_size());
        a.ym.save_state(blob.data());
        auto r1 = a.run(5000);
        uint8_t st1 = a.ym.read_status();
        a.ym.load_state(blob.data());
        auto r2 = a.run(5000);
        FM b;
        b.ym.load_state(blob.data());
        auto r3 = b.run(5000);
        check(r1 == r2 && r1 == r3 && st1 == b.ym.read_status(), "save/load: %zu-byte state reproduces output bit-exactly",
              blob.size());
    }
    {
        FM b;
        song_setup(b);
        std::vector<int32_t> rb;
        const int sizes[11] = {0, 1, 0, 3, 2, 0, 7, 1, 0, 0, 5};
        std::vector<int32_t> tmp(16);
        bool written = false;
        for (int done = 0, k = 0; done < 10000; k++) {
            int n = std::min(sizes[k % 11], (done < 5000 ? 5000 : 10000) - done);
            b.ym.generate(tmp.data(), n);
            rb.insert(rb.end(), tmp.begin(), tmp.begin() + 2 * n);
            done += n;
            if (done == 5000 && !written) { b.freq(1, 5, 300); b.key(2, 0); written = true; }
        }
        // same register writes at sample 5000 in a single-call render
        FM c;
        song_setup(c);
        auto rc = c.run(5000);
        c.freq(1, 5, 300);
        c.key(2, 0);
        auto rc2 = c.run(5000);
        rc.insert(rc.end(), rc2.begin(), rc2.end());
        check(rb == rc, "chunked generate (n = 0..7, writes between calls) == single-call render (10000 samples)");
    }
    {
        FM fm;
        sine_setup(fm, 4, 617);
        fm.run(2000);
        int lvl = fm.ym.channel_level[0];
        uint8_t ks = fm.ym.key_state(0);
        float f = fm.ym.channel_freq(0);
        fm.ym.generate(nullptr, 0);
        bool keep = fm.ym.channel_level[0] == lvl;
        fm.ym.channel_mute = 1;
        auto s = fm.run(100);
        bool silent = std::all_of(s.begin(), s.end(), [](int32_t v) { return v == 0; });
        fm.ym.channel_mute = 0;
        fm.key(0, 0);
        fm.run(3000);
        // negative peak: -8180 >> 5 = -256 -> level 8192
        check(lvl == 8192 && ks == 8 && std::fabs(f - ym_hz(617, 4)) < 0.01 && keep && silent &&
                  fm.ym.channel_freq(0) == 0 && fm.ym.key_state(0) == 0,
              "VU level %d, key_state 0x%X, channel_freq %.2f Hz, mute silences, freq 0 after release", lvl, ks, f);
    }
    {
        FM fm;
        song_setup(fm);
        std::vector<int32_t> buf(2 * 1024);
        auto t0 = std::chrono::steady_clock::now();
        int total = (int)FS * 20;
        for (int done = 0; done < total; done += 1024) fm.ym.generate(buf.data(), 1024);
        double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < (int)FS * 2; i++) { fm.ym.generate(buf.data(), 1); fm.ym.generate(buf.data(), 0); }
        double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(20 / s1 > 20 && 2 / s2 > 10, "performance: %.0fx real-time (1024-sample calls), %.0fx (n=1 + n=0 calls)",
              20 / s1, 2 / s2);
    }
}

// ---------------------------------------------------------------- PSG
struct SP {
    PSG p;
    void tone(int ch, int period) { p.write((uint8_t)(0x80 | (ch << 5) | (period & 15))); p.write((uint8_t)((period >> 4) & 0x3F)); }
    void vol(int ch, int v) { p.write((uint8_t)(0x90 | (ch << 5) | v)); }
    void noise(int mode) { p.write((uint8_t)(0xE0 | mode)); }
    std::vector<int32_t> run(int n, double rate = FS) {
        std::vector<int32_t> v((size_t)n);
        p.generate(v.data(), n, PSG_CLOCK, rate);
        return v;
    }
};
// Noise bits, one per LFSR shift, using a 1-tick-per-sample render (shift rate 0 = every 32 ticks).
static std::vector<int> noise_bits(SP& s, int count) {
    std::vector<int> bits;
    const double tick_rate = PSG_CLOCK / 16;
    auto x = s.run(33, tick_rate);
    bits.push_back(x[32] > 0);
    while ((int)bits.size() < count) {
        auto y = s.run(32 * 1024, tick_rate);
        for (int i = 31; i < 32 * 1024 && (int)bits.size() < count; i += 32) bits.push_back(y[i] > 0);
    }
    return bits;
}
static int find_period(const std::vector<int>& b, int maxp) {
    for (int p = 1; p <= maxp && p + 3000 < (int)b.size(); p++) {
        bool ok = true;
        for (size_t i = 0; i + p < b.size() && ok; i++) ok = b[i] == b[i + p];
        if (ok) return p;
    }
    return -1;
}

static void test_psg() {
    std::printf("\n== PSG (SN76489, Sega) ==\n");
    for (int period : {254, 1000, 57, 0x3FF, 0}) {
        for (double rate : {FS, 44100.0}) {
            SP s;
            s.tone(1, period);
            s.vol(1, 0);
            double f = measure_freq(to_d(s.run((int)rate, rate)), rate);
            double e = PSG_CLOCK / (32.0 * (period ? period : 0x400));
            check(std::fabs(f / e - 1) < 2e-4, "tone period %4d @ %5.0f Hz out: %8.3f Hz, expected clock/(32*N) = %8.3f",
                  period, rate, f, e);
        }
    }
    {
        SP s;
        s.tone(0, 254);
        s.vol(0, 0);
        auto a = s.run((int)FS / 2);
        write_wav("psg_tone_440hz.wav", a, 1, FS, 4.0);
        std::string lv;
        bool ok = true;
        double prev = 0;
        for (int v = 0; v < 16; v++) {
            s.vol(0, v);
            auto x = to_d(s.run(4000));
            double r = 0;
            for (double y : x) r += y * y;
            r = std::sqrt(r / x.size());
            if (v == 0 && std::fabs(r - 2600) > 60) ok = false;
            if (v > 0 && v < 15 && std::fabs(db(r / prev) + 2) > 0.1) ok = false;
            if (v == 15 && r != 0) ok = false;
            char b[16];
            std::snprintf(b, sizeof b, " %.0f", r);
            lv += b;
            prev = r;
        }
        check(ok, "volume 0..15 RMS:%s (2 dB steps, 15 = off)", lv.c_str());
    }
    {
        SP s;
        s.noise(0x0);   // periodic, N/512
        s.vol(3, 0);
        auto b = noise_bits(s, 4000);
        int ones = 0;
        for (int i = 0; i < 16; i++) ones += b[i];
        int p = find_period(b, 1000);
        check(p == 16 && ones == 1, "periodic noise: period %d shifts, %d one(s) per period", p, ones);
        SP w;
        w.noise(0x4);   // white, N/512
        w.vol(3, 0);
        auto wb = noise_bits(w, 130000);
        // reference: 16-bit Galois-free LFSR, feedback bit0 ^ bit3 into bit 15
        uint16_t r = 0x8000;
        std::vector<int> ref;
        for (int i = 0; i < 130000; i++) {
            ref.push_back(r & 1);
            int fb = (r ^ (r >> 3)) & 1;
            r = (uint16_t)((r >> 1) | (fb << 15));
        }
        int wp = find_period(wb, 70000);
        double ac = 0;
        for (int lag = 1; lag <= 32; lag++) {
            double c = 0;
            for (int i = 0; i < 60000; i++) c += (wb[i] * 2 - 1) * (wb[i + lag] * 2 - 1);
            ac = std::max(ac, std::fabs(c / 60000));
        }
        int off = -1;
        for (int o = 0; o < 4 && off < 0; o++) {
            bool m = true;
            for (int i = 0; i < 200; i++) m &= wb[i] == ref[i + o];
            if (m) off = o;
        }
        bool match = off >= 0;
        for (int i = 0; match && i < 100000; i++) match &= wb[i] == ref[i + off];
        check(wp > 16 && match && ac < 0.05,
              "white noise: sequence period %d shifts (taps 0x0009), matches reference LFSR, max |autocorr| lag 1-32 = %.3f",
              wp, ac);
        auto wa = w.run((int)FS / 2);
        write_wav("psg_noise_white.wav", wa, 1, FS, 4.0);
        s.noise(0x0);
        write_wav("psg_noise_periodic.wav", s.run((int)FS / 2), 1, FS, 4.0);
    }
    {
        std::string res;
        bool ok = true;
        for (int mode = 0; mode < 4; mode++) {
            SP s;
            s.tone(2, 10);
            s.vol(2, 15);
            s.noise(mode);   // periodic
            s.vol(3, 0);
            double f = measure_freq(to_d(s.run((int)FS, FS), 100), FS);
            double e = mode == 3 ? PSG_CLOCK / (32.0 * 10 * 16) : PSG_CLOCK / (512.0 * (1 << mode) * 16);
            if (std::fabs(f / e - 1) > 1e-3) ok = false;
            char b[48];
            std::snprintf(b, sizeof b, " %.2f (exp %.2f)", f, e);
            res += b;
        }
        check(ok, "periodic noise pitch for rates N/512, N/1024, N/2048, tone 3 (period 10):%s", res.c_str());
    }
    {
        SP s;
        s.p.write(0xB5);            // latch ch1 volume = 5
        s.p.write(0x0C);            // data byte -> volume low 4 bits = 12
        s.p.write(0xC5);            // latch ch2 tone low = 5
        s.p.write(0x3F);            // data -> high 6 bits
        s.p.write(0x01);            // data again (latched tone) -> high bits = 1
        int v1 = s.p.channel_volume(1), p2 = s.p.channel_period(2);
        s.p.write(0xE7);            // noise: white, tone-3 rate
        check(v1 == 12 && p2 == 0x015 && s.p.channel_period(3) == 0x015 && s.p.channel_volume(3) == 15,
              "latch/data protocol: vol %d, period 0x%03X, noise uses tone 3 period 0x%03X", v1, p2, s.p.channel_period(3));
    }
    {
        SP s;
        s.tone(0, 3);   // 37 kHz tone: above Nyquist, box filter averages it down
        s.vol(0, 0);
        auto x = to_d(s.run(4000));
        double pk = 0;
        for (double v : x) pk = std::max(pk, std::fabs(v));
        SP a;
        a.tone(0, 100); a.tone(1, 127); a.tone(2, 151);
        a.vol(0, 2); a.vol(1, 4); a.vol(2, 6); a.noise(5); a.vol(3, 8);
        a.run(1234);
        std::vector<uint8_t> blob(a.p.state_size());
        a.p.save_state(blob.data());
        auto r1 = a.run(5000);
        a.p.load_state(blob.data());
        auto r2 = a.run(5000);
        SP b;
        b.p.load_state(blob.data());
        auto r3 = b.run(5000);
        a.p.channel_mute = 0x0E;
        auto m = a.run(1000);
        bool muted_ok = a.p.channel_level[1] > 0;
        double r0 = 0;
        for (int v : m) r0 = std::max(r0, (double)std::abs(v));
        check(pk < 1200 && r1 == r2 && r1 == r3 && muted_ok && r0 <= a.p.channel_level[0] + 1,
              "ultrasonic tone peak %.0f (box filter), save/load bit-exact (%zu bytes), mute ok", pk, blob.size());
        auto t0 = std::chrono::steady_clock::now();
        std::vector<int32_t> buf(1024);
        for (int i = 0; i < 20 * 53; i++) a.p.generate(buf.data(), 1024, PSG_CLOCK, FS);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  PSG performance: %.0fx real-time\n", 20 * 53 * 1024 / FS / sec);
    }
}

int main() {
    std::filesystem::create_directories("tests/out");
    test_pitch();
    test_mul_dt();
    test_envelope();
    test_ssg();
    test_algorithms();
    test_dac();
    test_timers();
    test_ch3_special();
    test_lfo();
    test_state_api();
    test_psg();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
