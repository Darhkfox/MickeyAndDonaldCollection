#include "audio_mixer.h"
#include <cmath>
#include <cstring>

void AudioMixer::configure(double in_rate, double out_rate, AudioFilter filter, float volume) {
    in_rate_ = in_rate;
    out_rate_ = out_rate;
    volume_ = volume;
    double fc = filter == AudioFilter::Model1 ? 3390.0 : filter == AudioFilter::Model2 ? 7800.0 : 0.0;
    lp_coef_ = fc > 0 ? (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * fc / in_rate)) : 0.0f;
}

void AudioMixer::reset() {
    memset(lp_state_, 0, sizeof(lp_state_));
    memset(dc_x_, 0, sizeof(dc_x_));
    memset(dc_y_, 0, sizeof(dc_y_));
    memset(hist_, 0, sizeof(hist_));
    frac_ = 0.0;
}

static inline int16_t soft_limit(float x) {
    // Linear up to 24000, smooth knee towards 32767.
    const float knee = 24000.0f, room = 32767.0f - knee;
    float a = std::fabs(x);
    if (a > knee) a = knee + room * std::tanh((a - knee) / room);
    return (int16_t)(x < 0 ? -a : a);
}

void AudioMixer::process(const int32_t* in, int frames, double adjust, std::vector<int16_t>& out) {
    const double step = in_rate_ / out_rate_ / adjust;
    // Full-scale FM (6 loud channels) is ~49k, but the loudest music in the three games peaks near
    // 24k (boss themes), so at the default volume music peaks around 14k and leaves room for effects.
    const float gain = 0.9f * volume_;
    for (int i = 0; i < frames; i++) {
        float s[2];
        for (int c = 0; c < 2; c++) {
            float x = (float)in[i * 2 + c];
            if (lp_coef_ > 0) { lp_state_[c] += (x - lp_state_[c]) * lp_coef_; x = lp_state_[c]; }
            // DC blocker (~10 Hz)
            float y = x - dc_x_[c] + 0.9988f * dc_y_[c];
            dc_x_[c] = x; dc_y_[c] = y;
            s[c] = y * gain;
        }
        for (int k = 0; k < 3; k++) { hist_[k][0] = hist_[k + 1][0]; hist_[k][1] = hist_[k + 1][1]; }
        hist_[3][0] = s[0]; hist_[3][1] = s[1];
        while (frac_ < 1.0) {
            float t = (float)frac_;
            for (int c = 0; c < 2; c++) {
                // Catmull-Rom between hist[1] and hist[2]
                float p0 = hist_[0][c], p1 = hist_[1][c], p2 = hist_[2][c], p3 = hist_[3][c];
                float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
                out.push_back(soft_limit(v));
            }
            frac_ += step;
        }
        frac_ -= 1.0;
    }
}
