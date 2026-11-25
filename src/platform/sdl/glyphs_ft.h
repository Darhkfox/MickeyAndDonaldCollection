// UI glyph rasteriser for the GLES video layer on platforms without a text engine to call (desktop Linux; from
// simplePsyduck's Nintendo Switch port):
// FreeType over a list of fonts, with the same output contract as Android's MDActivity.renderGlyphs, so
// video_gles.cpp builds its glyph atlas the same way on both.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct FontBlob {
    const uint8_t* data;   // a whole TrueType / OpenType file in memory (must outlive the call)
    size_t size;
};

// Rasterises `cps` (Unicode code points) at `px` pixels, trying `faces` in order for each glyph.
//   meta[0] = ascent, meta[1] = line height (pixels)
//   meta[2 + 5*i ..] for cps[i]: advance * 64 (or -1: no font has the glyph), bitmap left offset and top offset
//                    from the pen / baseline (top is negative above the baseline), bitmap width, height
//   bits: the 8-bit coverage bitmaps of the glyphs with a non-empty bitmap, rows packed, in cps order
// Each bitmap carries a one-pixel margin. A few UI symbols (arrows, play triangles, check mark) that system fonts
// often lack are drawn by hand when no face has them. `bold` emboldens the outlines. False if no face loads.
bool ft_render_glyphs(const std::vector<FontBlob>& faces, const std::vector<uint32_t>& cps, float px, bool bold,
                      std::vector<int>& meta, std::vector<uint8_t>& bits);

