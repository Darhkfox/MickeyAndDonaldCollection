// Direct3D 11 presentation layer: game frame blit with filters + 2D overlay (rects, images, text).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct HWND__; typedef HWND__* HWND;

enum class ScaleFilter : int {
    Nearest = 0,     // crisp pixels
    SharpBilinear,   // integer prescale + bilinear (crisp but no shimmer)
    Bilinear,        // smooth
    Scanlines,       // sharp + horizontal scanline darkening
    CRT,             // aperture-grille style mask + scanlines + slight bloom
    Count
};

// Colours are 0xAARRGGBB.
struct Rect { float x, y, w, h; };

enum class FontId : int { Small = 0, Body, Title, Huge, Count };   // UI fonts
enum class Align : int { Left = 0, Center, Right };

class Video {
public:
    Video();
    ~Video();

    bool init(HWND hwnd, int width, int height);
    void shutdown();
    void resize(int width, int height);           // call on WM_SIZE
    int  width() const;
    int  height() const;
    void set_vsync(bool on);

    // ---- Game frame -------------------------------------------------------
    // Upload a BGRA8 frame (0xAARRGGBB little-endian in memory = B,G,R,A bytes).
    // Up to 512x256. Texture is recreated on size change.
    void upload_frame(const uint32_t* pixels, int w, int h, int pitch_pixels);
    // Upload the frame for a secondary texture slot (launcher previews). slot 1..3.
    void upload_frame_slot(int slot, const uint32_t* pixels, int w, int h, int pitch_pixels);

    // ---- Per-frame drawing (call between begin_frame / end_frame) --------
    void begin_frame(uint32_t clear_color);
    // Draw the uploaded game frame (slot 0 = main) into `dst` (pixels in backbuffer
    // space) using `filter`. src_w/src_h: source size of the frame in texels
    // (normally the uploaded size). `alpha` fades it, `dim` darkens (0..1).
    void draw_game(int slot, Rect dst, ScaleFilter filter, float alpha = 1.0f, float dim = 0.0f);

    void fill_rect(Rect r, uint32_t color);
    void fill_rect_gradient(Rect r, uint32_t top, uint32_t bottom);
    void fill_round_rect(Rect r, float radius, uint32_t color);   // anti-aliased edges
    void outline_round_rect(Rect r, float radius, float thickness, uint32_t color);

    // Text is rendered from glyph atlases built at init via GDI/DirectWrite.
    // `scale` multiplies the font's native pixel size. Supports printable ASCII
    // plus: U+2190-2193 (arrows), U+25B6 (play), U+2713 (check), U+2022 (bullet),
    // U+266A (note), U+00A9 (copyright), U+2026 (ellipsis). Text is UTF-8.
    void draw_text(FontId font, float x, float y, const std::string& utf8,
                   uint32_t color, Align align = Align::Left, float scale = 1.0f);
    float measure_text(FontId font, const std::string& utf8, float scale = 1.0f);
    float line_height(FontId font, float scale = 1.0f);

    // Font sizes are chosen relative to backbuffer height; call after resize
    // (done automatically inside resize()).
    void rebuild_fonts();

    // Read back the current backbuffer as 0xAARRGGBB pixels (screenshots / tests).
    // Call after drawing and before end_frame(); flushes pending overlay draws.
    bool capture(std::vector<uint32_t>& pixels, int& w, int& h);

    void end_frame();   // Present

private:
    struct Impl;
    Impl* impl;
};
