#include "palette.h"
#include <cmath>
#include <algorithm>

namespace {
// Levels measured from real Mega Drive video output.
const float kNormal[8] = {0, 52, 87, 116, 144, 172, 206, 255};
const float kShadow[8] = {0, 29, 52, 70, 87, 101, 116, 130};
const float kHigh[8]   = {130, 144, 158, 172, 187, 206, 228, 255};

float level(ColorMode mode, int inten, int v) {
    if (mode == ColorMode::Linear) {
        float n = v * 255.0f / 7.0f;
        return inten == 0 ? n : inten == 1 ? n * 0.5f : 127.5f + n * 0.5f;
    }
    return inten == 0 ? kNormal[v] : inten == 1 ? kShadow[v] : kHigh[v];
}
inline float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
}

void build_palette(uint32_t lut[2048], ColorMode mode, int brightness, int saturation, int gamma) {
    const float bright = brightness / 100.0f;             // -0.5 .. 0.5
    float sat = 1.0f + saturation / 100.0f;               // 0 .. 2
    const float gam = std::pow(2.0f, -gamma / 50.0f);     // 0.5 .. 2
    float contrast = 1.0f;
    switch (mode) {
    case ColorMode::Vivid: sat *= 1.35f; contrast = 1.08f; break;
    case ColorMode::Grayscale: case ColorMode::Sepia: case ColorMode::Handheld: sat = 0; break;
    default: break;
    }
    for (int in = 0; in < 3; in++) {
        for (int c = 0; c < 512; c++) {
            float r = level(mode, in, c & 7) / 255.0f;
            float g = level(mode, in, (c >> 3) & 7) / 255.0f;
            float b = level(mode, in, (c >> 6) & 7) / 255.0f;
            // saturation around Rec.601 luma
            float y = 0.299f * r + 0.587f * g + 0.114f * b;
            r = y + (r - y) * sat; g = y + (g - y) * sat; b = y + (b - y) * sat;
            // contrast around mid grey, brightness, gamma
            r = (r - 0.5f) * contrast + 0.5f + bright;
            g = (g - 0.5f) * contrast + 0.5f + bright;
            b = (b - 0.5f) * contrast + 0.5f + bright;
            r = std::pow(clamp01(r), gam); g = std::pow(clamp01(g), gam); b = std::pow(clamp01(b), gam);
            switch (mode) {
            case ColorMode::Warm:
                r = clamp01(r * 1.04f + 0.02f); g = clamp01(g * 0.99f + 0.015f); b = clamp01(b * 0.88f + 0.02f);
                break;
            case ColorMode::Cool:
                r = clamp01(r * 0.94f); g = clamp01(g * 0.99f); b = clamp01(b * 1.06f + 0.01f);
                break;
            case ColorMode::Sepia: {
                float l = r;   // already grey
                r = clamp01(l * 1.07f + 0.03f); g = clamp01(l * 0.86f + 0.02f); b = clamp01(l * 0.62f + 0.01f);
                break;
            }
            case ColorMode::Handheld: {
                static const float shades[4][3] = {{0.06f, 0.22f, 0.06f}, {0.19f, 0.38f, 0.19f},
                                                   {0.55f, 0.67f, 0.06f}, {0.61f, 0.74f, 0.06f}};
                int s = std::min(3, (int)(r * 4.0f));
                r = shades[s][0]; g = shades[s][1]; b = shades[s][2];
                break;
            }
            default: break;
            }
            uint32_t R = (uint32_t)(clamp01(r) * 255.0f + 0.5f);
            uint32_t G = (uint32_t)(clamp01(g) * 255.0f + 0.5f);
            uint32_t B = (uint32_t)(clamp01(b) * 255.0f + 0.5f);
            lut[(in << 9) | c] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
    // Code 0x600-0x7FF (intensity 3) never occurs; keep it defined.
    for (int i = 1536; i < 2048; i++) lut[i] = lut[i - 1536];
}
