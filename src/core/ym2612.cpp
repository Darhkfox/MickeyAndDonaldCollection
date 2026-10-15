// Yamaha YM2612 (OPN2) - sample-accurate model (one sample per 144 input clocks).
// Operator/EG/LFO structure follows the classic OPN model with the hardware
// corrections found by Nemesis (SSG-EG, EG rate table, CSM, detune overflow,
// LFO reset state) and the DAC ladder effect of the discrete Model 1 chip.
#include "ym2612.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace {

enum : uint8_t { EG_OFF, EG_REL, EG_SUS, EG_DEC, EG_ATT };

// EG increment patterns (8-step cycles), rows selected per rate.
const uint8_t eg_inc[19][8] = {
    {0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1}, {1,1,1,2,1,1,1,2}, {1,2,1,2,1,2,1,2}, {1,2,2,2,1,2,2,2},
    {2,2,2,2,2,2,2,2}, {2,2,2,4,2,2,2,4}, {2,4,2,4,2,4,2,4}, {2,4,4,4,2,4,4,4},
    {4,4,4,4,4,4,4,4}, {4,4,4,8,4,4,4,8}, {4,8,4,8,4,8,4,8}, {4,8,8,8,4,8,8,8},
    {8,8,8,8,8,8,8,8}, {16,16,16,16,16,16,16,16}, {0,0,0,0,0,0,0,0},
};

// LFO PM displacement per F-number bit (bits 4..10), per PMS depth, per LFO step (1/4 wave).
const uint8_t lfo_pm_output[7][8][8] = {
    { {0},{0},{0},{0},{0},{0},{0}, {0,0,0,0,1,1,1,1} },
    { {0},{0},{0},{0},{0},{0}, {0,0,0,0,1,1,1,1}, {0,0,1,1,2,2,2,3} },
    { {0},{0},{0},{0},{0}, {0,0,0,0,1,1,1,1}, {0,0,1,1,2,2,2,3}, {0,0,2,3,4,4,5,6} },
    { {0},{0},{0}, {0,0,0,0,1,1,1,1}, {0,0,0,0,1,1,1,1}, {0,0,1,1,2,2,2,3},
      {0,0,2,3,4,4,5,6}, {0,0,4,6,8,8,10,12} },
    { {0}, {0,0,0,0,1,1,1,1}, {0,0,0,1,1,1,1,2}, {0,0,1,1,2,2,2,3}, {0,0,1,2,2,2,3,4},
      {0,0,2,3,4,4,5,6}, {0,0,4,6,8,8,10,12}, {0,0,8,12,16,16,20,24} },
    { {0}, {0,0,0,0,2,2,2,2}, {0,0,0,2,2,2,4,4}, {0,0,2,2,4,4,4,6}, {0,0,2,4,4,4,6,8},
      {0,0,4,6,8,8,10,12}, {0,0,8,12,16,16,20,24}, {0,0,16,24,32,32,40,48} },
    { {0}, {0,0,0,0,4,4,4,4}, {0,0,0,4,4,4,8,8}, {0,0,4,4,8,8,8,12}, {0,0,4,8,8,8,12,16},
      {0,0,8,12,16,16,20,24}, {0,0,16,24,32,32,40,48}, {0,0,32,48,64,64,80,96} },
};

const uint8_t fktable[16] = {0,0,0,0,0,0,0,1,2,3,3,3,3,3,3,3};   // N4/N3 from fnum bits 10..7
const uint8_t lfo_period[8] = {108, 77, 71, 67, 62, 44, 8, 5};    // samples per LFO step
const uint8_t ams_shift[4] = {8, 3, 1, 0};                         // 0 / 1.4 / 5.9 / 11.8 dB

struct Tables {
    uint16_t sinrom[256];     // -log2(sin) in 4.8 fixed point, quarter wave
    uint16_t exprom[256];     // 2^x mantissa (10 bits)
    uint8_t  dt[4][32];       // detune in basefreq units, by DT(0..3) and keycode
    int16_t  pm[128][8][32];  // LFO PM offset (1/2 fnum units) by fnum bits 10..4, PMS, LFO step
    uint8_t  eg_shift[64], eg_sel[64];
    Tables() {
        const double PI = 3.14159265358979323846;
        for (int i = 0; i < 256; i++) {
            sinrom[i] = (uint16_t)std::lround(-std::log2(std::sin((2 * i + 1) * PI / 1024.0)) * 256.0);
            exprom[i] = (uint16_t)std::lround((std::pow(2.0, i / 256.0) - 1.0) * 1024.0);
        }
        // Detune: base value from note bits, scaled by octave (die-verified form of the dt table).
        static const uint8_t dtb[8] = {16, 17, 19, 20, 22, 24, 27, 29};
        for (int d = 0; d < 4; d++)
            for (int kc = 0; kc < 32; kc++) {
                if (!d) { dt[d][kc] = 0; continue; }
                int k = kc > 28 ? 28 : kc;
                int sum = (k >> 2) + 9 + (d == 3 ? 3 : d == 2 ? 2 : 0);
                dt[d][kc] = (uint8_t)(dtb[((sum & 1) << 2) | (k & 3)] >> (9 - (sum >> 1)));
            }
        for (int f = 0; f < 128; f++)
            for (int d = 0; d < 8; d++)
                for (int st = 0; st < 8; st++) {
                    int v = 0;
                    for (int b = 0; b < 7; b++)
                        if (f & (1 << b)) v += lfo_pm_output[b][d][st];
                    pm[f][d][st] = (int16_t)v;
                    pm[f][d][(st ^ 7) + 8] = (int16_t)v;
                    pm[f][d][st + 16] = (int16_t)-v;
                    pm[f][d][(st ^ 7) + 24] = (int16_t)-v;
                }
        for (int r = 0; r < 64; r++) {
            eg_shift[r] = (uint8_t)(r < 48 ? 11 - (r >> 2) : 0);
            if (r < 2)       eg_sel[r] = 18;            // rates 0/1: no change
            else if (r < 6)  eg_sel[r] = 0;             // rates 2..7 per Nemesis' measurements
            else if (r < 8)  eg_sel[r] = 2;
            else if (r < 48) eg_sel[r] = (uint8_t)(r & 3);
            else if (r < 60) eg_sel[r] = (uint8_t)(4 + ((r - 48) >> 2) * 4 + (r & 3));
            else             eg_sel[r] = 16;
        }
    }
};
const Tables T;

struct Op {
    uint32_t phase, inc;       // 20-bit phase accumulator and increment
    int32_t  volume;           // 10-bit EG attenuation
    uint16_t fnum; uint8_t block, kc;
    uint8_t  state, key, ssgn; // key = register key-on, ssgn = SSG-EG inversion (0/4)
    uint8_t  dt, mul, tl, ks, ar, am, d1r, d2r, sl, rr, ssg;
};

struct Ch {
    Op op[4];                  // register slot order: op1 (+0), op3 (+4), op2 (+8), op4 (+12)
    uint16_t fnum; uint8_t block;
    uint8_t alg, fb, pan_l, pan_r, ams, pms;
    int32_t op1_out[2], mem;
};

struct State {
    Ch ch[6];
    uint16_t addr;
    uint8_t  fn_latch, sl3_latch;
    uint16_t sl3_fnum[3]; uint8_t sl3_block[3];
    uint8_t  mode, status, csm_key, dac_en, freq_dirty;
    uint16_t ta_reg, ta_cnt, tb_cnt; uint8_t tb_reg, tb_sub;
    int32_t  busy, dac_out;
    uint32_t eg_cnt, eg_timer, lfo_timer;
    uint8_t  lfo_on, lfo_rate, lfo_cnt, lfo_am, lfo_pm;
};

inline int op_calc(uint32_t phase, int mod, int att) {
    uint32_t p = ((phase >> 10) + (uint32_t)mod) & 0x3FF;
    uint32_t q = (p & 0x100) ? (~p & 0xFF) : (p & 0xFF);
    int level = T.sinrom[q] + (att << 2);
    if (level >= 13 * 256) return 0;
    int out = ((T.exprom[(level & 0xFF) ^ 0xFF] | 0x400) << 2) >> (level >> 8);
    return (p & 0x200) ? -out : out;
}

} // namespace

struct YM2612::Impl {
    State s;

    static int rate(const Op& o, int r5) {
        if (!r5) return 0;
        int r = 2 * r5 + (o.kc >> (3 - o.ks));
        return r > 63 ? 63 : r;
    }
    static int sl_att(const Op& o) { return o.sl == 15 ? 0x3E0 : o.sl << 5; }
    static bool inverted(const Op& o) { return (o.ssg & 8) && ((o.ssgn ^ o.ssg) & 4); }

    static uint32_t calc_inc(const Op& o, int pm) {
        uint32_t f = (((uint32_t)o.fnum << 1) + (uint32_t)pm) & 0xFFF;
        uint32_t base = (f << o.block) >> 2;
        uint32_t d = T.dt[o.dt & 3][o.kc];
        base = ((o.dt & 4) ? base - d : base + d) & 0x1FFFF;   // detune can wrap (overflow)
        return ((base * (o.mul ? o.mul * 2u : 1u)) >> 1) & 0xFFFFF;
    }

    void update_freq(int c) {
        Ch& ch = s.ch[c];
        bool special = c == 2 && (s.mode & 0xC0);
        static const int sl3_idx[3] = {1, 0, 2};   // op1 <- 0xA9, op3 <- 0xA8, op2 <- 0xAA
        for (int i = 0; i < 4; i++) {
            Op& o = ch.op[i];
            if (special && i < 3) { o.fnum = s.sl3_fnum[sl3_idx[i]]; o.block = s.sl3_block[sl3_idx[i]]; }
            else                  { o.fnum = ch.fnum; o.block = ch.block; }
            o.kc = (uint8_t)((o.block << 2) | fktable[o.fnum >> 7]);
            o.inc = calc_inc(o, 0);
        }
    }

    void keyon(Op& o) {
        o.phase = 0;
        o.ssgn = 0;
        if (rate(o, o.ar) < 62)
            o.state = o.volume <= 0 ? (sl_att(o) == 0 ? EG_SUS : EG_DEC) : EG_ATT;
        else {
            o.volume = 0;
            o.state = sl_att(o) == 0 ? EG_SUS : EG_DEC;
        }
    }
    void keyoff(Op& o) {
        if (o.state <= EG_REL) return;
        o.state = EG_REL;
        if (o.ssg & 8) {
            if ((o.ssgn ^ o.ssg) & 4) o.volume = (0x200 - o.volume) & 0x3FF;
            if (o.volume >= 0x200) { o.volume = 0x3FF; o.state = EG_OFF; }
        }
    }
    // Effective key = register key OR CSM key (channel 3 only).
    void set_key(int c, int slot, bool on) {
        Op& o = s.ch[c].op[slot];
        bool csm = c == 2 && (s.csm_key & 1);
        bool before = o.key || csm, after = on || csm;
        o.key = on;
        if (!before && after) keyon(o);
        else if (before && !after) keyoff(o);
    }
    void csm_off() {
        for (Op& o : s.ch[2].op) if (!o.key) keyoff(o);
    }

    void write_mode(uint8_t v) {
        if ((s.mode ^ v) & 0xC0) {
            s.freq_dirty |= 4;
            if ((v & 0xC0) != 0x80 && (s.csm_key & 1)) { s.csm_key = 0; csm_off(); }
        }
        if ((v & 1) && !(s.mode & 1)) s.ta_cnt = s.ta_reg;
        if ((v & 2) && !(s.mode & 2)) s.tb_cnt = s.tb_reg;
        s.status &= (uint8_t)(~v >> 4) & 3;
        s.mode = v;
    }

    void write_reg(int a, uint8_t v) {
        int part = a >> 8, r = a & 0xFF;
        if (r < 0x30) {
            if (part) return;
            switch (r) {
            case 0x22:
                if (v & 8) { s.lfo_on = 1; s.lfo_rate = v & 7; }
                else { s.lfo_on = 0; s.lfo_timer = 0; s.lfo_cnt = 0; s.lfo_pm = 0; s.lfo_am = 126; }
                break;
            case 0x24: s.ta_reg = (uint16_t)((s.ta_reg & 3) | (v << 2)); break;
            case 0x25: s.ta_reg = (uint16_t)((s.ta_reg & 0x3FC) | (v & 3)); break;
            case 0x26: s.tb_reg = v; break;
            case 0x27: write_mode(v); break;
            case 0x28: {
                int c = v & 3;
                if (c == 3) break;
                if (v & 4) c += 3;
                static const int slot_of_bit[4] = {0, 2, 1, 3};   // bits 4..7 = op1, op2, op3, op4
                for (int i = 0; i < 4; i++) set_key(c, slot_of_bit[i], (v >> (4 + i)) & 1);
                break;
            }
            case 0x2A: s.dac_out = ((int)v - 128) << 6; break;
            case 0x2B: s.dac_en = v >> 7; break;
            }
            return;
        }
        int c = r & 3;
        if (c == 3) return;
        int cn = c + part * 3;
        Ch& ch = s.ch[cn];
        if (r < 0xA0) {
            Op& o = ch.op[(r >> 2) & 3];
            switch (r & 0xF0) {
            case 0x30: o.dt = (v >> 4) & 7; o.mul = v & 15; s.freq_dirty |= 1 << cn; break;
            case 0x40: o.tl = v & 0x7F; break;
            case 0x50: o.ks = v >> 6; o.ar = v & 0x1F; break;
            case 0x60: o.am = v >> 7; o.d1r = v & 0x1F; break;
            case 0x70: o.d2r = v & 0x1F; break;
            case 0x80: o.sl = v >> 4; o.rr = v & 15; break;
            case 0x90: o.ssg = v & 15; break;
            }
            return;
        }
        switch (r & 0xFC) {
        case 0xA0:   // low byte commits the latched block/fnum high byte
            ch.fnum = (uint16_t)(((s.fn_latch & 7) << 8) | v);
            ch.block = (s.fn_latch >> 3) & 7;
            s.freq_dirty |= 1 << cn;
            break;
        case 0xA4: s.fn_latch = v & 0x3F; break;
        case 0xA8:
            if (part) break;
            s.sl3_fnum[c] = (uint16_t)(((s.sl3_latch & 7) << 8) | v);
            s.sl3_block[c] = (s.sl3_latch >> 3) & 7;
            s.freq_dirty |= 4;
            break;
        case 0xAC: if (!part) s.sl3_latch = v & 0x3F; break;
        case 0xB0: ch.alg = v & 7; ch.fb = (v >> 3) & 7; break;
        case 0xB4:
            ch.pan_l = v >> 7; ch.pan_r = (v >> 6) & 1;
            ch.ams = (v >> 4) & 3; ch.pms = v & 7;
            break;
        }
    }

    void reset() {
        std::memset(&s, 0, sizeof(s));
        for (Ch& ch : s.ch) {
            ch.pan_l = ch.pan_r = 1;
            for (Op& o : ch.op) { o.volume = 0x3FF; o.state = EG_OFF; }
        }
        s.lfo_am = 126;   // LFO held in reset: AM output is at its maximum
        s.freq_dirty = 0x3F;
    }

    // SSG-EG loop/hold/alternate handling, evaluated every sample.
    void update_ssg(Op& o) {
        if (!(o.ssg & 8) || o.volume < 0x200 || o.state <= EG_REL) return;
        if (o.ssg & 1) {                       // hold
            if (o.ssg & 2) o.ssgn = 4;
            if (o.state != EG_ATT) {
                // hold low: force silence; hold high: pin the (inverted) level at 0 dB so a
                // decay step overshooting 0x200 can't wrap the inverted output
                if (!((o.ssgn ^ o.ssg) & 4)) o.volume = 0x3FF;
                else o.volume = 0x200;
            }
        } else {                               // loop
            if (o.ssg & 2) o.ssgn ^= 4;
            else o.phase = 0;
            if (o.state != EG_ATT) {
                if (rate(o, o.ar) < 62)
                    o.state = o.volume <= 0 ? (sl_att(o) == 0 ? EG_SUS : EG_DEC) : EG_ATT;
                else { o.volume = 0; o.state = sl_att(o) == 0 ? EG_SUS : EG_DEC; }
            }
        }
    }

    void eg_step(Op& o) {
        int r;
        switch (o.state) {
        case EG_ATT: r = rate(o, o.ar); break;
        case EG_DEC: r = rate(o, o.d1r); break;
        case EG_SUS: r = rate(o, o.d2r); break;
        case EG_REL: r = rate(o, o.rr * 2 + 1); break;
        default: return;
        }
        int sh = T.eg_shift[r];
        if (s.eg_cnt & ((1u << sh) - 1)) return;
        int inc = eg_inc[T.eg_sel[r]][(s.eg_cnt >> sh) & 7];
        bool ssg = (o.ssg & 8) != 0;
        switch (o.state) {
        case EG_ATT:
            o.volume += (~o.volume * inc) >> 4;
            if (o.volume <= 0) { o.volume = 0; o.state = sl_att(o) == 0 ? EG_SUS : EG_DEC; }
            break;
        case EG_DEC:
            if (ssg) { if (o.volume < 0x200) o.volume += 4 * inc; }
            else o.volume += inc;
            if (o.volume >= sl_att(o)) o.state = EG_SUS;
            break;
        case EG_SUS:
            if (ssg) { if (o.volume < 0x200) o.volume += 4 * inc; }
            else if ((o.volume += inc) > 0x3FF) o.volume = 0x3FF;
            break;
        case EG_REL:
            if (ssg) {
                if (o.volume < 0x200) o.volume += 4 * inc;
                if (o.volume >= 0x200) { o.volume = 0x3FF; o.state = EG_OFF; }
            } else if ((o.volume += inc) >= 0x3FF) { o.volume = 0x3FF; o.state = EG_OFF; }
            break;
        }
    }

    static int eg_out(const Op& o, int am) {
        int v = o.volume;
        if (o.state > EG_REL && inverted(o)) v = (0x200 - v) & 0x3FF;
        v += (o.tl << 3) + (o.am ? am : 0);
        return v > 0x3FF ? 0x3FF : v;
    }

    // One sample of a channel: operators run in the order op1, op3, op2, op4; op1's
    // output reaches its targets one sample late and MEM delays one more sample.
    int calc_ch(Ch& ch) {
        int am = s.lfo_am >> ams_shift[ch.ams];
        int alg = ch.alg, m2 = 0, c1 = 0, c2 = 0, mem = 0, out = 0;
        if (alg <= 2 || alg == 5) m2 = ch.mem;
        else if (alg == 3) c2 = ch.mem;
        Op* o = ch.op;

        int fbin = ch.op1_out[0] + ch.op1_out[1];
        int v1 = ch.op1_out[0] = ch.op1_out[1];
        switch (alg) {
        case 1: mem = v1; break;
        case 2: c2 = v1; break;
        case 5: mem = c1 = c2 = v1; break;
        case 7: out = v1; break;
        default: c1 = v1; break;
        }
        int a = eg_out(o[0], am);
        ch.op1_out[1] = a < 0x340 ? op_calc(o[0].phase, ch.fb ? fbin >> (10 - ch.fb) : 0, a) : 0;
        if ((a = eg_out(o[1], am)) < 0x340) {
            int v = op_calc(o[1].phase, m2 >> 1, a);
            if (alg <= 4) c2 += v; else out += v;
        }
        if ((a = eg_out(o[2], am)) < 0x340) {
            int v = op_calc(o[2].phase, c1 >> 1, a);
            if (alg <= 3) mem += v; else out += v;
        }
        if ((a = eg_out(o[3], am)) < 0x340) out += op_calc(o[3].phase, c2 >> 1, a);
        ch.mem = mem;

        for (int i = 0; i < 4; i++) {
            uint32_t inc = o[i].inc;
            if (ch.pms) {
                int pm = T.pm[(o[i].fnum >> 4) & 0x7F][ch.pms][s.lfo_pm];
                if (pm) inc = calc_inc(o[i], pm);
            }
            o[i].phase = (o[i].phase + inc) & 0xFFFFF;
        }
        return out > 8191 ? 8191 : out < -8192 ? -8192 : out;
    }

    void tick_timers() {
        uint8_t prev_csm = s.csm_key & 1;
        s.csm_key = 0;
        if ((s.mode & 1) && ++s.ta_cnt >= 1024) {
            s.ta_cnt = s.ta_reg;
            if (s.mode & 4) s.status |= 1;
            if ((s.mode & 0xC0) == 0x80) {        // CSM: key-on all ch3 operators for one sample
                s.csm_key = 1;
                if (!prev_csm) for (Op& o : s.ch[2].op) if (!o.key) keyon(o);
            }
        }
        if (prev_csm && !s.csm_key) csm_off();
        s.tb_sub = (s.tb_sub + 1) & 15;           // free-running /16 prescaler
        if (!s.tb_sub && (s.mode & 2) && ++s.tb_cnt >= 256) {
            s.tb_cnt = s.tb_reg;
            if (s.mode & 8) s.status |= 2;
        }
    }
};

YM2612::YM2612() : impl(new Impl) { impl->reset(); }
YM2612::~YM2612() { delete impl; }
void YM2612::reset() { impl->reset(); }

void YM2612::write(int port, uint8_t v) {
    State& s = impl->s;
    switch (port & 3) {
    case 0: s.addr = v; break;
    case 2: s.addr = (uint16_t)(0x100 | v); break;
    default: impl->write_reg(s.addr, v); s.busy = 32; break;   // busy for 32 FM cycles
    }
}

uint8_t YM2612::read_status() {
    return (uint8_t)(impl->s.status | (impl->s.busy > 0 ? 0x80 : 0));
}

void YM2612::generate(int32_t* out, int n) {
    if (n <= 0) return;
    State& s = impl->s;
    for (int c = 0; c < 6; c++) channel_level[c] = 0;
    for (int i = 0; i < n; i++) {
        if (s.freq_dirty) {
            for (int c = 0; c < 6; c++) if (s.freq_dirty & (1 << c)) impl->update_freq(c);
            s.freq_dirty = 0;
        }
        for (Ch& ch : s.ch) for (Op& o : ch.op) impl->update_ssg(o);
        int L = 0, R = 0;
        for (int c = 0; c < 6; c++) {
            Ch& ch = s.ch[c];
            int v = impl->calc_ch(ch);
            if (c == 5 && s.dac_en) v = s.dac_out;
            v >>= 5;                               // 9-bit DAC
            int lv = std::abs(v * 32);
            if (lv > channel_level[c]) channel_level[c] = lv;
            if ((channel_mute >> c) & 1) continue;
            if (ladder_effect) {
                // Model 1 DAC: +4 offset for v >= 0; -3 (panned) / -4 (not panned) for v < 0.
                if (v >= 0) { L += (ch.pan_l ? v + 4 : 4) * 32; R += (ch.pan_r ? v + 4 : 4) * 32; }
                else        { L += (ch.pan_l ? v - 3 : -4) * 32; R += (ch.pan_r ? v - 3 : -4) * 32; }
            } else {
                if (ch.pan_l) L += v * 32;
                if (ch.pan_r) R += v * 32;
            }
        }
        out[2 * i] = L;
        out[2 * i + 1] = R;

        if (s.lfo_on && ++s.lfo_timer >= lfo_period[s.lfo_rate]) {
            s.lfo_timer = 0;
            s.lfo_cnt = (s.lfo_cnt + 1) & 127;
            s.lfo_am = (uint8_t)((s.lfo_cnt < 64 ? (s.lfo_cnt ^ 63) : (s.lfo_cnt & 63)) << 1);
            s.lfo_pm = s.lfo_cnt >> 2;
        }
        if (++s.eg_timer >= 3) {
            s.eg_timer = 0;
            if (++s.eg_cnt == 4096) s.eg_cnt = 1;
            for (Ch& ch : s.ch) for (Op& o : ch.op) impl->eg_step(o);
        }
        impl->tick_timers();
    }
    s.busy -= 24 * n;
    if (s.busy < 0) s.busy = 0;
}

uint8_t YM2612::key_state(int ch) const {
    if (ch < 0 || ch > 5) return 0;
    const State& s = impl->s;
    const Op* o = s.ch[ch].op;
    bool csm = ch == 2 && (s.csm_key & 1);
    return (uint8_t)(((o[0].key || csm) ? 1 : 0) | ((o[2].key || csm) ? 2 : 0) |
                     ((o[1].key || csm) ? 4 : 0) | ((o[3].key || csm) ? 8 : 0));
}

float YM2612::channel_freq(int ch) const {
    if (ch < 0 || ch > 5) return 0.0f;
    const State& s = impl->s;
    if (ch == 5 && s.dac_en) return 0.0f;
    const Ch& c = s.ch[ch];
    bool active = key_state(ch) != 0;
    for (const Op& o : c.op) if (o.state != EG_OFF && o.volume < 0x380) active = true;
    if (!active) return 0.0f;
    return (float)(c.fnum * std::ldexp(1.0, c.block - 1) * (7670453.0 / 144.0) / 1048576.0);
}

size_t YM2612::state_size() const { return sizeof(State); }
void YM2612::save_state(uint8_t* dst) const { std::memcpy(dst, &impl->s, sizeof(State)); }
void YM2612::load_state(const uint8_t* src) { std::memcpy(&impl->s, src, sizeof(State)); }
