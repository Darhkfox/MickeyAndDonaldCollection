// UI glyphs through FreeType (desktop Linux): from simplePsyduck's Nintendo Switch port of this collection
// (github.com/simplePsyduck/MickeyAndDonaldCollection-NX), its system-font part left out.
#include "glyphs_ft.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H


namespace {

// ---- hand-drawn symbols ----------------------------------------------------------------------------------
struct Seg { float x0, y0, x1, y1; };   // in a unit square, y down

float dist_to_seg(float px, float py, const Seg& s) {
    const float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((px - s.x0) * dx + (py - s.y0) * dy) / len2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    const float cx = s.x0 + t * dx - px, cy = s.y0 + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

bool in_triangle(float px, float py, const float t[6]) {
    auto side = [](float ax, float ay, float bx, float by, float x, float y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); };
    const float d1 = side(t[0], t[1], t[2], t[3], px, py);
    const float d2 = side(t[2], t[3], t[4], t[5], px, py);
    const float d3 = side(t[4], t[5], t[0], t[1], px, py);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Draws one of the symbols into a (s+2) x (s+2) coverage bitmap (4x4 supersampling). False if `cp` isn't one.
bool synth_glyph(uint32_t cp, float px, int& adv64, int& ox, int& oy, int& w, int& h, std::vector<uint8_t>& out) {
    Seg segs[3] = {};
    int nseg = 0;
    float tri[6] = {};
    bool is_tri = false;
    float thick = 0.0f;
    const int s = std::max(6, (int)std::lround(px * 0.62f));
    switch (cp) {
    case 0x25B6: is_tri = true; tri[0] = 0.10f; tri[1] = 0.05f; tri[2] = 0.95f; tri[3] = 0.50f; tri[4] = 0.10f; tri[5] = 0.95f; break;
    case 0x25C0: is_tri = true; tri[0] = 0.90f; tri[1] = 0.05f; tri[2] = 0.05f; tri[3] = 0.50f; tri[4] = 0.90f; tri[5] = 0.95f; break;
    case 0x2713: segs[0] = {0.08f, 0.55f, 0.38f, 0.88f}; segs[1] = {0.38f, 0.88f, 0.95f, 0.12f}; nseg = 2; thick = std::max(1.6f, s * 0.17f); break;
    case 0x2190: segs[0] = {0.05f, 0.5f, 0.95f, 0.5f}; segs[1] = {0.05f, 0.5f, 0.42f, 0.12f}; segs[2] = {0.05f, 0.5f, 0.42f, 0.88f}; nseg = 3; thick = std::max(1.3f, s * 0.12f); break;
    case 0x2192: segs[0] = {0.05f, 0.5f, 0.95f, 0.5f}; segs[1] = {0.95f, 0.5f, 0.58f, 0.12f}; segs[2] = {0.95f, 0.5f, 0.58f, 0.88f}; nseg = 3; thick = std::max(1.3f, s * 0.12f); break;
    case 0x2191: segs[0] = {0.5f, 0.05f, 0.5f, 0.95f}; segs[1] = {0.5f, 0.05f, 0.12f, 0.42f}; segs[2] = {0.5f, 0.05f, 0.88f, 0.42f}; nseg = 3; thick = std::max(1.3f, s * 0.12f); break;
    case 0x2193: segs[0] = {0.5f, 0.05f, 0.5f, 0.95f}; segs[1] = {0.5f, 0.95f, 0.12f, 0.58f}; segs[2] = {0.5f, 0.95f, 0.88f, 0.58f}; nseg = 3; thick = std::max(1.3f, s * 0.12f); break;
    default: return false;
    }
    w = h = s + 2;
    out.assign((size_t)w * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int hit = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    const float ux = (x - 1 + (i + 0.5f) / 4) / s, uy = (y - 1 + (j + 0.5f) / 4) / s;   // unit square
                    if (ux < -0.02f || ux > 1.02f || uy < -0.02f || uy > 1.02f) continue;
                    if (is_tri) {
                        hit += in_triangle(ux, uy, tri);
                    } else {
                        for (int k = 0; k < nseg; k++) {
                            const Seg& sg = segs[k];
                            if (dist_to_seg(ux * s, uy * s, {sg.x0 * s, sg.y0 * s, sg.x1 * s, sg.y1 * s}) <= thick * 0.5f) { hit++; break; }
                        }
                    }
                }
            out[(size_t)y * w + x] = (uint8_t)(hit * 255 / 16);
        }
    adv64 = (s + 2 + s / 8) * 64;
    ox = 0;
    oy = -(s + 1) - (int)std::lround(px * 0.04f);   // the shape stands on the baseline, a little raised
    return true;
}

}  // namespace

bool ft_render_glyphs(const std::vector<FontBlob>& faces, const std::vector<uint32_t>& cps, float px, bool bold,
                      std::vector<int>& meta, std::vector<uint8_t>& bits) {
    meta.assign(2 + cps.size() * 5, 0);
    bits.clear();
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0) return false;
    std::vector<FT_Face> open;
    for (const FontBlob& b : faces) {
        FT_Face f = nullptr;
        if (b.data && b.size && FT_New_Memory_Face(lib, b.data, (FT_Long)b.size, 0, &f) == 0) {
            if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)std::max(1.0f, std::round(px))) == 0) open.push_back(f);
            else FT_Done_Face(f);
        }
    }
    if (open.empty()) { FT_Done_FreeType(lib); return false; }

    const FT_Size_Metrics& pm = open[0]->size->metrics;
    const int ascent = (int)std::ceil(pm.ascender / 64.0), descent = (int)std::ceil(-pm.descender / 64.0);
    meta[0] = ascent;
    meta[1] = ascent + descent;

    for (size_t i = 0; i < cps.size(); i++) {
        int* m = &meta[2 + i * 5];
        const uint32_t cp = cps[i];
        FT_Face face = nullptr;
        FT_UInt gi = 0;
        for (FT_Face f : open)
            if ((gi = FT_Get_Char_Index(f, cp)) != 0) { face = f; break; }
        if (!face) {
            int adv, ox, oy, w, h;
            std::vector<uint8_t> px8;
            if (synth_glyph(cp, px, adv, ox, oy, w, h, px8)) {
                m[0] = adv; m[1] = ox; m[2] = oy; m[3] = w; m[4] = h;
                bits.insert(bits.end(), px8.begin(), px8.end());
            } else {
                m[0] = -1;
            }
            continue;
        }
        if (FT_Load_Glyph(face, gi, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0) { m[0] = -1; continue; }
        FT_GlyphSlot slot = face->glyph;
        if (bold && slot->format == FT_GLYPH_FORMAT_OUTLINE) FT_GlyphSlot_Embolden(slot);
        m[0] = (int)slot->advance.x;   // 26.6 fixed point: pixels * 64, as the contract wants
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) continue;
        const FT_Bitmap& bm = slot->bitmap;
        if (bm.width == 0 || bm.rows == 0 || !bm.buffer) continue;   // (m[1..4] stay 0: an empty glyph, e.g. a space)
        const int w = (int)bm.width + 2, h = (int)bm.rows + 2;       // a pixel of room on each side
        m[1] = slot->bitmap_left - 1;
        m[2] = -slot->bitmap_top - 1;
        m[3] = w;
        m[4] = h;
        const size_t base = bits.size();
        bits.resize(base + (size_t)w * h, 0);
        for (int y = 0; y < (int)bm.rows; y++) {
            const uint8_t* row = bm.pitch >= 0 ? bm.buffer + (size_t)y * bm.pitch : bm.buffer + (size_t)(bm.rows - 1 - y) * (size_t)(-bm.pitch);
            if (bm.pixel_mode == FT_PIXEL_MODE_GRAY) {
                memcpy(&bits[base + (size_t)(y + 1) * w + 1], row, bm.width);
            } else if (bm.pixel_mode == FT_PIXEL_MODE_MONO) {
                for (unsigned x = 0; x < bm.width; x++) bits[base + (size_t)(y + 1) * w + 1 + x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
            }
        }
    }
    for (FT_Face f : open) FT_Done_Face(f);
    FT_Done_FreeType(lib);
    return true;
}

