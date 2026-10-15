// Platform layer test: game-frame filters, overlay primitives, text, audio tone and live input.
//   platform_test                      interactive (F1 filter, F2 overlay, F3 vsync, Esc quit)
//   platform_test --selftest out.png [--size WxH] [--filter N] [--nooverlay] [--frames N]
//                                      hidden window, renders a few frames, writes the backbuffer as PNG
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/platform/video.h"
#include "../src/platform/audio.h"
#include "../src/platform/input.h"

static Video g_video;
static Audio g_audio;
static Input g_input;
static bool g_running = true, g_selftest = false;

static const char* const kFilterNames[] = { "Nearest", "Sharp Bilinear", "Bilinear", "Scanlines", "CRT" };

// ---- minimal PNG writer (stored deflate blocks) --------------------------------------
static uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static bool write_png(const char* path, const std::vector<uint32_t>& px, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(((size_t)w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);   // filter: none
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x];
            raw.push_back((uint8_t)(c >> 16)); raw.push_back((uint8_t)(c >> 8)); raw.push_back((uint8_t)c);
        }
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    size_t pos = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        uint8_t last = pos + n == raw.size() ? 1 : 0;
        uint8_t hdr[5] = { last, (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)~n, (uint8_t)(~n >> 8) };
        z.insert(z.end(), hdr, hdr + 5);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size();) {
        size_t end = std::min(raw.size(), i + 5552);
        for (; i < end; i++) { a += raw[i]; b += a; }
        a %= 65521; b %= 65521;
    }
    uint32_t adler = (b << 16) | a;
    uint8_t ad[4] = { (uint8_t)(adler >> 24), (uint8_t)(adler >> 16), (uint8_t)(adler >> 8), (uint8_t)adler };
    z.insert(z.end(), ad, ad + 4);

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    auto be32 = [](uint8_t* o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; };
    auto chunk = [&](const char* type, const uint8_t* data, size_t n) {
        uint8_t len[4], crcb[4];
        be32(len, (uint32_t)n);
        uint32_t c = crc32((const uint8_t*)type, 4, 0);
        c = crc32(data, n, c);
        be32(crcb, c);
        fwrite(len, 1, 4, f); fwrite(type, 1, 4, f);
        if (n) fwrite(data, 1, n, f);
        fwrite(crcb, 1, 4, f);
    };
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;   // 8-bit RGB
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), z.size());
    chunk("IEND", nullptr, 0);
    bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

// ---- test patterns ------------------------------------------------------------------
static void make_pattern(std::vector<uint32_t>& px, int W, int H, int frame) {
    px.assign((size_t)W * H, 0);
    auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < W && y < H) px[(size_t)y * W + x] = c; };
    auto rect = [&](int x0, int y0, int w, int h, uint32_t c) {
        for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) put(x, y, c);
    };
    for (int y = 0; y < H; y++)       // scrolling checkerboard background
        for (int x = 0; x < W; x++)
            px[(size_t)y * W + x] = ((((x + frame) >> 3) ^ ((y + frame / 2) >> 3)) & 1) ? 0xFF2C4A8C : 0xFF1A2A55;
    static const uint32_t bars[8] = { 0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
                                      0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000 };
    for (int i = 0; i < 8; i++) rect(8 + i * 24, 8, 24, 40, bars[i]);
    for (int x = 0; x < 192; x++) { uint32_t v = (uint32_t)(x * 255 / 191); rect(8 + x, 48, 1, 12, 0xFF000000 | v << 16 | v << 8 | v); }
    rect(6, 62, 200, 36, 0xFF000000);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            put(8 + x, 64 + y, ((x ^ y) & 1) ? 0xFFFFFFFF : 0xFF000000);            // 1px checker
            put(44 + x, 64 + y, (x & 1) ? 0xFFFFFFFF : 0xFF000000);                 // vertical lines
            put(80 + x, 64 + y, (y & 1) ? 0xFFFFFFFF : 0xFF000000);                 // horizontal lines
            put(116 + x, 64 + y, (((x >> 1) ^ (y >> 1)) & 1) ? 0xFFFF4040 : 0xFF40FF40);
            put(152 + x, 64 + y, ((x + y) % 4 == 0) ? 0xFFFFFFFF : 0xFF000000);     // diagonals
        }
    for (int y = -44; y <= 44; y++)   // big circle with outline
        for (int x = -44; x <= 44; x++) {
            int d = x * x + y * y;
            if (d <= 44 * 44) put(300 + x, 58 + y, d >= 41 * 41 ? 0xFF000000 : (y < -20 ? 0xFFFFF080 : 0xFFFFC020));
        }
    for (int y = 120; y < 200; y++)   // diagonal stripes
        for (int x = 230; x < 390; x++)
            if (((x - y + 400) / 6) % 2) put(x, y, 0xFFE05050);
    int sx = (frame * 2) % (W + 16) - 16;   // moving sprite
    rect(sx, 180, 16, 16, 0xFFFFFFFF);
    rect(sx + 2, 182, 12, 12, 0xFFD02020);
    for (int x = 0; x < W; x++) { put(x, 0, 0xFFFFFFFF); put(x, H - 1, 0xFFFFFFFF); }
    for (int y = 0; y < H; y++) { put(0, y, 0xFFFFFFFF); put(W - 1, y, 0xFFFFFFFF); }
}

static void make_preview(std::vector<uint32_t>& px, int W, int H) {
    px.resize((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t r = (uint32_t)(x * 255 / W), g = (uint32_t)(y * 255 / H), b = 160;
            int dx = x - W / 2, dy = y - H / 2;
            if (dx * dx + dy * dy < 60 * 60) r = g = b = 240;
            px[(size_t)y * W + x] = 0xFF000000 | r << 16 | g << 8 | b;
        }
}

// ---- frame rendering ----------------------------------------------------------------
static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) { if (!s.empty()) s += ", "; s += x; }
    return s.empty() ? "-" : s;
}

static void render(int filter, bool overlay, double fps, bool vsync) {
    Video& v = g_video;
    const float W = (float)v.width(), H = (float)v.height(), u = H / 720.0f;
    const uint32_t white = 0xFFFFFFFF, grey = 0xFFB8C0CC, accent = 0xFF2F6FEB;
    v.begin_frame(0xFF0C0F14);
    float s = std::min(W / 400.0f, H / 224.0f);
    v.draw_game(0, { (W - 400 * s) * 0.5f, (H - 224 * s) * 0.5f, 400 * s, 224 * s }, (ScaleFilter)filter);

    char buf[256];
    snprintf(buf, sizeof(buf), "Filter: %s (F1)    %dx%d  %.0f fps  vsync %s", kFilterNames[filter],
             (int)W, (int)H, fps, vsync ? "on" : "off");
    float bh = v.line_height(FontId::Body) + 10 * u;
    v.fill_round_rect({ 12 * u, H - bh - 12 * u, v.measure_text(FontId::Body, buf) + 28 * u, bh }, bh * 0.5f, 0xC0000000);
    v.draw_text(FontId::Body, 26 * u, H - bh - 7 * u, buf, white);
    if (!overlay) return;

    // Fonts panel.
    Rect pn = { W - 480 * u, 16 * u, 464 * u, H - 32 * u };
    v.fill_round_rect(pn, 18 * u, 0xE8161B26);
    v.outline_round_rect(pn, 18 * u, 1.5f * u, 0x60FFFFFF);
    Rect hdr = { pn.x + 14 * u, pn.y + 14 * u, pn.w - 28 * u, v.line_height(FontId::Title) + 8 * u };
    v.fill_rect_gradient(hdr, 0xFF4A7FE8, 0xFF182E66);
    v.draw_text(FontId::Title, hdr.x + hdr.w * 0.5f, hdr.y + 4 * u, "Castle of Illusion", white, Align::Center);
    float cx = pn.x + pn.w * 0.5f, x0 = pn.x + 24 * u, x1 = pn.x + pn.w - 24 * u;
    float y = hdr.y + hdr.h + 6 * u;
    v.draw_text(FontId::Huge, cx, y, "HUGE 1990", 0xFFFFD34D, Align::Center);
    y += v.line_height(FontId::Huge);
    v.draw_text(FontId::Title, x0, y, "Title Font Wg", white); y += v.line_height(FontId::Title);
    // UTF-8 as byte escapes (MSVC without /utf-8 mangles u8"\u..." literals).
    v.draw_text(FontId::Body, x0, y, "Body: Press \xE2\x96\xB6 Start \xE2\x80\xA2 Options", white); y += v.line_height(FontId::Body);
    v.draw_text(FontId::Small, x0, y, "Small: The quick brown fox jumps over the lazy dog.", grey); y += v.line_height(FontId::Small);
    v.draw_text(FontId::Body, x0, y, "\xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x92 \xE2\x86\x93 \xE2\x96\xB6 \xE2\x97\x80 \xE2\x9C\x93 "
                "\xE2\x80\xA2 \xE2\x99\xAA \xC2\xA9 \xE2\x80\xA6 \xE2\x80\x94 \xE2\x82\xAC", white);
    y += v.line_height(FontId::Body);
    v.draw_text(FontId::Body, x0, y, "\xC3\x84" "pfel \xC3\x96l \xC3\x9C" "bung \xC3\x9F \xC3\xA9 \xC3\xB1 \xC2\xBF \xC2\xBD \xC2\xB0", white);
    y += v.line_height(FontId::Body) + 4 * u;
    v.fill_rect({ cx - 0.5f, y, 1, v.line_height(FontId::Small) * 3 }, 0x80FF4040);
    v.fill_rect({ x0 - 0.5f, y, 1, v.line_height(FontId::Small) * 3 }, 0x80FF4040);
    v.fill_rect({ x1 - 0.5f, y, 1, v.line_height(FontId::Small) * 3 }, 0x80FF4040);
    v.draw_text(FontId::Small, x0, y, "Left aligned", white, Align::Left); y += v.line_height(FontId::Small);
    v.draw_text(FontId::Small, cx, y, "Centered", white, Align::Center); y += v.line_height(FontId::Small);
    v.draw_text(FontId::Small, x1, y, "Right aligned", white, Align::Right); y += v.line_height(FontId::Small) + 10 * u;
    float bw = (pn.w - 60 * u) * 0.5f, bth = v.line_height(FontId::Body) + 12 * u;
    Rect b1 = { x0, y, bw, bth }, b2 = { x1 - bw, y, bw, bth };
    v.fill_round_rect(b1, bth * 0.5f, accent);
    v.outline_round_rect(b1, bth * 0.5f, 2 * u, white);
    v.draw_text(FontId::Body, b1.x + bw * 0.5f, y + 6 * u, "\xE2\x96\xB6 Continue", white, Align::Center);
    v.fill_round_rect(b2, bth * 0.5f, 0x30FFFFFF);
    v.draw_text(FontId::Body, b2.x + bw * 0.5f, y + 6 * u, "Settings", grey, Align::Center);
    y += bth + 12 * u;
    // Launcher-style previews from slot 1: normal, faded, dimmed.
    float tw = (pn.w - 48 * u - 16 * u) / 3, th = tw * 224.0f / 320.0f;
    const char* labels[3] = { "normal", "alpha 0.5", "dim 0.6" };
    for (int i = 0; i < 3; i++) {
        Rect t = { x0 + i * (tw + 8 * u), y, tw, th };
        v.draw_game(1, t, ScaleFilter::Bilinear, i == 1 ? 0.5f : 1.0f, i == 2 ? 0.6f : 0.0f);
        v.outline_round_rect({ t.x - 2 * u, t.y - 2 * u, t.w + 4 * u, t.h + 4 * u }, 6 * u, 2 * u, i == 0 ? accent : 0x50FFFFFF);
        v.draw_text(FontId::Small, t.x + tw * 0.5f, y + th + 4 * u, labels[i], grey, Align::Center);
    }

    // Input panel.
    std::vector<std::string> lines;
    std::vector<std::string> keys;
    for (int k = 1; k < 256; k++) if (g_input.key_down(k)) keys.push_back(Input::key_name(k));
    lines.push_back("Keys held: " + join(keys));
    static int last_key = 0;
    if (g_input.last_key_pressed()) last_key = g_input.last_key_pressed();
    lines.push_back("Last key pressed: " + (last_key ? Input::key_name(last_key) : std::string("-")));
    int npads = 0;
    for (int i = 0; i < g_input.pad_count(); i++) {
        const PadState& p = g_input.pad(i);
        if (!p.connected) continue;
        npads++;
        std::vector<std::string> btn;
        for (int bit = 0; bit < 24; bit++) if (p.buttons & (1u << bit)) btn.push_back(Input::pad_button_name(1u << bit));
        snprintf(buf, sizeof(buf), "[%d] %s  (%.2f, %.2f)  ", i, p.name.c_str(), p.lx, p.ly);
        std::string line = buf + join(btn);
        if (!p.is_xinput && p.raw_buttons) {
            line += "  raw:";
            for (int bit = 0; bit < 64; bit++) if (p.raw_buttons & (1ull << bit)) line += " " + std::to_string(bit);
        }
        lines.push_back(line);
    }
    if (!npads) lines.push_back("No controllers connected");
    snprintf(buf, sizeof(buf), "first_connected_pad() = %d", g_input.first_connected_pad());
    lines.push_back(buf);
    float lh = v.line_height(FontId::Small);
    float ih = v.line_height(FontId::Body) + lh * lines.size() + 24 * u;
    Rect ip = { 16 * u, H - bh - 24 * u - ih, W - 480 * u - 32 * u, ih };
    v.fill_round_rect(ip, 12 * u, 0xD0101010);
    v.outline_round_rect(ip, 12 * u, 1 * u, 0x40FFFFFF);
    float iy = ip.y + 10 * u;
    v.draw_text(FontId::Body, ip.x + 16 * u, iy, "Input \xE2\x99\xAA", 0xFF8FB8FF);
    iy += v.line_height(FontId::Body);
    for (auto& l : lines) { v.draw_text(FontId::Small, ip.x + 16 * u, iy, l, white); iy += lh; }
    float r = 14 * u, rx = ip.x + ip.w - 16 * u, ry = ip.y + 14 * u;   // AA check: disc, 1px ring, thick ring
    v.fill_round_rect({ rx - 2 * r, ry, 2 * r, 2 * r }, r, 0xFFFF6040);
    v.outline_round_rect({ rx - 4.5f * r, ry, 2 * r, 2 * r }, r, 1.0f, white);
    v.outline_round_rect({ rx - 7 * r, ry, 2 * r, 2 * r }, r, 4.0f * u, 0xFF40C0FF);
    v.draw_text(FontId::Small, rx - 8 * r, ry + 2 * r + 6 * u, "scale 1.5 \xE2\x9C\x93", 0xFF8FB8FF, Align::Right, 1.5f);
}

// ---- window -------------------------------------------------------------------------
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        g_input.on_key((int)wp, true);
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;   // keep Alt+F4
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP: g_input.on_key((int)wp, false); return 0;
    case WM_SYSCHAR: return 0;                             // no menu beep
    case WM_KILLFOCUS: g_input.on_focus_lost(); return 0;
    case WM_DEVICECHANGE: g_input.on_device_change(); return TRUE;
    case WM_SIZE:
        if (!g_selftest && wp != SIZE_MINIMIZED) g_video.resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_CLOSE: g_running = false; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void fill_audio(int& generated) {
    int16_t buf[4800 * 2];
    int want = 4800 - g_audio.queued_frames();   // keep ~100 ms queued
    if (want <= 0) return;
    want = std::min(want, 4800);
    for (int i = 0; i < want; i++, generated++) {
        double t = generated / 48000.0, env = 0;
        if (generated < 48000) env = std::min(1.0, std::min(t, 1.0 - t) / 0.02);   // 1 s, 20 ms fades
        int16_t s = (int16_t)(std::sin(2 * 3.14159265358979 * 440.0 * t) * env * 0.08 * 32767);
        buf[i * 2] = buf[i * 2 + 1] = s;
    }
    g_audio.push(buf, want);
}

int main(int argc, char** argv) {
    const char* out = nullptr;
    int W = 1280, H = 720, filter = (int)ScaleFilter::SharpBilinear, frames = 3;
    bool overlay = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf_s(argv[++i], "%dx%d", &W, &H);
        else if (!strcmp(argv[i], "--filter") && i + 1 < argc) filter = atoi(argv[++i]) % (int)ScaleFilter::Count;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--nooverlay")) overlay = false;
    }
    g_selftest = out != nullptr;
    SetProcessDPIAware();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PlatformTest";
    RegisterClassExW(&wc);
    DWORD style = g_selftest ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, W, H };
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Platform Test", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { fprintf(stderr, "CreateWindow failed\n"); return 1; }

    // The selftest starts at half size and resizes, to exercise ResizeBuffers + font rebuild.
    if (!g_video.init(hwnd, g_selftest ? W / 2 : W, g_selftest ? H / 2 : H)) {
        fprintf(stderr, "video init failed\n"); DestroyWindow(hwnd); return 1;
    }
    bool audio_ok = g_audio.init(48000);
    g_input.init(hwnd);

    std::vector<uint32_t> frame_px, preview;
    make_preview(preview, 320, 224);
    g_video.upload_frame_slot(1, preview.data(), 320, 224, 320);

    if (g_selftest) {
        int rc_code = 0;
        // Audio: push silence, check queue accounting and the 250 ms cap.
        if (audio_ok) {
            std::vector<int16_t> silence(20000 * 2, 0);
            g_audio.push(silence.data(), 4800);
            int q0 = g_audio.queued_frames();
            Sleep(60);
            int q1 = g_audio.queued_frames();
            g_audio.push(silence.data(), 20000);
            int q2 = g_audio.queued_frames();
            printf("audio: queued %d -> %d after 60 ms, %d after overfill (cap %d)\n", q0, q1, q2, g_audio.sample_rate() / 4);
            if (q0 <= 0 || q1 >= q0 || q2 > g_audio.sample_rate() / 4) { printf("audio: FAIL\n"); rc_code = 1; }
        } else printf("audio: init failed (no device?)\n");
        g_input.poll();
        printf("input: first_connected_pad=%d, keys: %s | %s | %s | %s | %s\n", g_input.first_connected_pad(),
               Input::key_name(VK_UP).c_str(), Input::key_name(VK_NEXT).c_str(), Input::key_name('Q').c_str(),
               Input::key_name(VK_OEM_1).c_str(), Input::key_name(VK_NUMPAD5).c_str());
        for (int i = 0; i < Input::kMaxPads; i++)
            if (g_input.pad(i).connected) printf("  pad %d: %s\n", i, g_input.pad(i).name.c_str());
        std::vector<uint32_t> shot;
        int sw = 0, sh = 0;
        make_pattern(frame_px, 400, 224, 0);
        g_video.upload_frame(frame_px.data(), 400, 224, 400);
        render(filter, overlay, 60.0, true);
        g_video.end_frame();
        g_video.resize(W, H);
        for (int f = 0; f < frames; f++) {
            make_pattern(frame_px, 400, 224, f * 3);
            g_video.upload_frame(frame_px.data(), 400, 224, 400);
            render(filter, overlay, 60.0, true);
            if (f == frames - 1 && !g_video.capture(shot, sw, sh)) { printf("capture failed\n"); rc_code = 1; }
            g_video.end_frame();
        }
        if (!shot.empty()) {
            if (write_png(out, shot, sw, sh)) printf("wrote %s (%dx%d, filter %s)\n", out, sw, sh, kFilterNames[filter]);
            else { printf("failed to write %s\n", out); rc_code = 1; }
        }
        g_input.shutdown(); g_audio.shutdown(); g_video.shutdown();
        DestroyWindow(hwnd);
        return rc_code;
    }

    ShowWindow(hwnd, SW_SHOW);
    bool vsync = true;
    int generated = 0, frame = 0;
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    double fps = 0;
    int fps_frames = 0;
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!g_running) break;
        if (IsIconic(hwnd)) { Sleep(16); continue; }   // Present doesn't block while minimized
        g_input.poll();
        if (g_input.key_pressed(VK_ESCAPE)) break;
        if (g_input.key_pressed(VK_F1)) filter = (filter + 1) % (int)ScaleFilter::Count;
        if (g_input.key_pressed(VK_F2)) overlay = !overlay;
        if (g_input.key_pressed(VK_F3)) { vsync = !vsync; g_video.set_vsync(vsync); }
        if (audio_ok) fill_audio(generated);
        make_pattern(frame_px, 400, 224, frame++);
        g_video.upload_frame(frame_px.data(), 400, 224, 400);
        render(filter, overlay, fps, vsync);
        g_video.end_frame();
        fps_frames++;
        QueryPerformanceCounter(&t1);
        double dt = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        if (dt >= 0.5) { fps = fps_frames / dt; fps_frames = 0; t0 = t1; }
    }
    g_input.shutdown(); g_audio.shutdown(); g_video.shutdown();
    DestroyWindow(hwnd);
    return 0;
}
