// Launcher: game cards with live attract-mode previews, plus Options / Music Player / Credits / Quit.
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
std::string narrow(const std::wstring& w) { return to_utf8(w); }
const int kButtonCount = 4;
#ifdef __ANDROID__
// Android apps don't quit from a menu (Back on the launcher still offers it); ROMs come in through the picker.
const char* kButtons[kButtonCount] = {"Options", "Music Player", "Credits", "Add ROMs"};
#else
const char* kButtons[kButtonCount] = {"Options", "Music Player", "Credits", "Quit"};
#endif

// Shortens a file name to `max_w` by cutting the end of its stem ("World of Illusion Starring Mickey<...>.bin").
std::string elide_name(Video& v, FontId font, const std::string& name, float max_w, float scale) {
    if (v.measure_text(font, name, scale) <= max_w) return name;
    size_t dot = name.find_last_of('.');
    std::string ext = dot != std::string::npos && name.size() - dot <= 5 ? name.substr(dot) : "";
    std::string stem = name.substr(0, name.size() - ext.size());
    while (stem.size() > 4) {
        while (!stem.empty() && ((unsigned char)stem.back() & 0xC0) == 0x80) stem.pop_back();   // a UTF-8 character's tail
        if (!stem.empty()) stem.pop_back();                                                       // and its first byte
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        if (v.measure_text(font, stem + "\xE2\x80\xA6" + ext, scale) <= max_w) break;
    }
    return stem + "\xE2\x80\xA6" + ext;
}
}

void App::update_launcher(const UiInput& ui, double dt) {
    const int N = kGameCount;          // cards 0..N-1, buttons N..N+3
    poll_roms(dt);
    int& s = launcher_sel_;
    if (s < N) launcher_last_card_ = s;
    if (ui.left && s != 0 && s != N) s--;
    if (ui.right && s != N - 1 && s != N + kButtonCount - 1) s++;
    if (ui.down && s < N) s = N + 1;
    if (ui.up && s >= N) s = launcher_last_card_;
    bool go = ui.confirm;
    if (ui.tap)   // touch: a card or button is chosen and used at once
        for (int i = 0; i < N + kButtonCount; i++) {
            const Rect& r = launcher_hit_[i];
            if (ui.tap_x >= r.x && ui.tap_x < r.x + r.w && ui.tap_y >= r.y && ui.tap_y < r.y + r.h) { s = i; go = true; }
        }
    if (s < N) launcher_last_card_ = s;
    if (go) {
        if (s < N) launch(s);
        else if (s == N) open_options_menu(true);
        else if (s == N + 1) {
            int g = -1;
            for (int k = 0; k < N && g < 0; k++) {
                int c = (launcher_last_card_ + k) % N;
                if (games_[c].rom.found) g = c;
            }
            if (g >= 0) open_jukebox(g, false);
            else menu_.toast("No ROMs found");
        } else if (s == N + 2) open_credits(true);
#ifdef __ANDROID__
        else pick_roms();
#else
        else running_ = false;
#endif
    }
    if (ui.menu || ui.back) menu_.confirm("Quit?", [this]() { running_ = false; });
    for (int i = 0; i < N + kButtonCount; i++) {
        float target = i == s ? 1.0f : 0.0f;
        launcher_anim_[i] += (target - launcher_anim_[i]) * (float)std::min(1.0, dt * 12.0);
    }
}

void App::draw_launcher() {
    const float W = (float)video_.width(), H = (float)video_.height();
    const int N = kGameCount;
    uint32_t acc = accent();

    // Background: black with a fine slate grid, centred so it stays symmetric at any window size.
    video_.fill_rect({0, 0, W, H}, 0xFF060708);
    const float cell = std::round(H / 14.0f), line = std::max(1.0f, std::round(H / 336.0f));
    const uint32_t grid = 0xFF333B45;
    for (float x = std::fmod(W * 0.5f, cell) - line * 0.5f; x < W; x += cell) video_.fill_rect({x, 0, line, H}, grid);
    for (float y = std::fmod(H * 0.5f, cell) - line * 0.5f; y < H; y += cell) video_.fill_rect({0, y, W, line}, grid);

    // Title
    float ty0 = H * 0.035f;
    float tscale = std::min(1.0f, W * 0.86f / std::max(1.0f, video_.measure_text(FontId::Huge, "Mickey & Donald Collection")));
    tscale = std::min(tscale, 0.8f);
    video_.draw_text(FontId::Huge, W * 0.5f, ty0, "Mickey & Donald Collection", 0xFFF6F1E4, Align::Center, tscale);
    ty0 += video_.line_height(FontId::Huge, tscale);
#ifdef __ANDROID__
    const char* edition = "Android Edition";
#else
    const char* edition = "PC Edition";
#endif
    video_.draw_text(FontId::Body, W * 0.5f, ty0,
                     std::string("Sega Mega Drive / Genesis classics  \xE2\x80\xA2  ") + edition + "  \xE2\x80\xA2  v" + kAppVersion,
                     0xFF9AA4BA, Align::Center);
    ty0 += video_.line_height(FontId::Body);

    // Cards
    const float top = ty0 + H * 0.045f;
    float gap = W * 0.03f;
    // The text under the preview is sized by the window height (fonts), the preview by the card width: give the
    // text the room its lines need and fit the card width to what is left (narrow 4:3 windows).
    const float rad0 = H * 0.02f;
    const float info_h = rad0 * 0.7f + video_.line_height(FontId::Title) * 0.95f +
                         video_.line_height(FontId::Small) * (1.05f + 1.1f + 1.0f) + rad0 * 0.6f;
    float cw = std::min((W * 0.94f - gap * (N - 1)) / N,
                        (H * 0.82f - top - info_h - rad0 * 0.6f) * 320.0f / 224.0f + rad0 * 1.2f);
    float ph = rad0 * 0.6f + (cw - rad0 * 1.2f) * 224.0f / 320.0f;
    float ch = ph + info_h;
    float x0 = (W - (cw * N + gap * (N - 1))) * 0.5f;
    // ROM file names share one text size (the one the longest name needs)
    std::string rom_label[kGameCount];
    float rom_fs = 1.0f;
    for (int i = 0; i < N; i++) {
        if (!games_[i].rom.found) continue;
        const std::wstring& path = games_[i].rom.path;
        rom_label[i] = "\xE2\x9C\x93 " + narrow(path.substr(path.find_last_of(L"\\/") + 1));
        rom_fs = std::min(rom_fs, cw * 0.92f / std::max(1.0f, video_.measure_text(FontId::Small, rom_label[i])));
    }
    for (int i = 0; i < N; i++) {
        GameSlot& g = games_[i];
        float an = launcher_anim_[i];
        float sc = 1.0f + 0.035f * an;
        float cx = x0 + i * (cw + gap) + cw * 0.5f, cy = top + ch * 0.5f;
        Rect r = {cx - cw * sc * 0.5f, cy - ch * sc * 0.5f, cw * sc, ch * sc};
        launcher_hit_[i] = r;
        uint32_t c = g.module->accent_color();
        float rad = H * 0.02f;
        if (an > 0.01f) {
            float glow = H * 0.012f * an;
            video_.fill_round_rect({r.x - glow, r.y - glow, r.w + glow * 2, r.h + glow * 2}, rad + glow, with_alpha(c, 0.25f * an));
        }
        video_.fill_round_rect(r, rad, 0xFF141824);
        Rect pr = {r.x + rad * 0.6f, r.y + rad * 0.6f, r.w - rad * 1.2f, (r.w - rad * 1.2f) * 224.0f / 320.0f};
        if (g.preview) {
            draw_game_view(1 + i, *g.preview, pr, 1.0f, 0.35f * (1.0f - an));
        } else {
            video_.fill_round_rect(pr, rad * 0.5f, 0xFF0A0D14);
            // What is missing (or which file is the wrong version), shrunk to fit the preview area.
            const bool other = !g.rom.other_file.empty();
            const std::string head = other ? "Wrong ROM version" : "ROM not found";
            char crc[64];
            snprintf(crc, sizeof(crc), "(CRC32 %08X, %u KB%s).", g.module->rom_crc32(), g.module->rom_size() / 1024,
                     other ? "" : "; zipped is fine");
            std::vector<std::string> lines;
            if (other) {
                lines.push_back("is " + g.rom.other_desc + ".");
                lines.push_back(std::string("Needs ") + g.module->rom_hint());
            } else {
#ifdef __ANDROID__
                lines.push_back("Tap Add ROMs and choose");
                lines.push_back(g.module->rom_hint());
#else
                lines.push_back(std::string("Place ") + g.module->rom_hint());
                lines.push_back("in the game folder or a \"roms\" folder");
#endif
            }
            lines.push_back(crc);
            float bw = 1.0f;
            for (const auto& l : lines) bw = std::max(bw, video_.measure_text(FontId::Small, l));
            if (other) {
                const std::wstring& f = g.rom.other_file;
                lines.insert(lines.begin(), elide_name(video_, FontId::Small, narrow(f.substr(f.find_last_of(L"\\/") + 1)), bw, 1.0f));
            }
            float hs = std::min(1.0f, pr.w * 0.9f / std::max(1.0f, video_.measure_text(FontId::Title, head)));
            float bs = std::min(1.0f, pr.w * 0.9f / bw);
            float th = video_.line_height(FontId::Title, hs) * 1.15f + video_.line_height(FontId::Small, bs) * lines.size();
            const float vs = std::min(1.0f, pr.h * 0.9f / th);
            hs *= vs;
            bs *= vs;
            float y = pr.y + (pr.h - th * vs) * 0.5f;
            video_.draw_text(FontId::Title, pr.x + pr.w * 0.5f, y, head, other ? 0xFFF0B060 : 0xFFE06A6A, Align::Center, hs);
            y += video_.line_height(FontId::Title, hs) * 1.15f;
            for (const auto& l : lines) {
                video_.draw_text(FontId::Small, pr.x + pr.w * 0.5f, y, l, 0xFFB0B8C8, Align::Center, bs);
                y += video_.line_height(FontId::Small, bs);
            }
        }
        float ty = pr.y + pr.h + rad * 0.7f;
        float tfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Title, g.module->title())));
        video_.draw_text(FontId::Title, r.x + r.w * 0.5f, ty, g.module->title(), an > 0.5f ? c : 0xFFE6E9F0, Align::Center, tfs);
        ty += video_.line_height(FontId::Title, tfs) * 0.95f;
        std::string sub = g.module->subtitle();
        float sfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Small, sub)));
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, sub, 0xFF9AA4BA, Align::Center, sfs);
        ty += video_.line_height(FontId::Small) * 1.05f;
        std::string meta = std::string(g.module->year()) + "  \xE2\x80\xA2  " +
                           (g.module->max_players() > 1 ? "1-2 Players" : "1 Player");
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, meta,
                         g.module->max_players() > 1 ? lerp_color(c, 0xFFFFFFFF, 0.3f) : 0xFF7C8598, Align::Center);
        ty += video_.line_height(FontId::Small) * 1.1f;
        if (g.rom.found) video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, rom_label[i], 0xFF6FCF8A, Align::Center, rom_fs);
        video_.outline_round_rect(r, rad, std::max(1.5f, H / 400.0f) * (0.5f + an), with_alpha(an > 0.01f ? c : 0xFF2A3040, 0.6f + 0.4f * an));
    }

    // Buttons
    float by = H * 0.855f;
    float bh = video_.line_height(FontId::Body) * 1.6f;
    float bws[kButtonCount], total = 0;
    for (int i = 0; i < kButtonCount; i++) { bws[i] = video_.measure_text(FontId::Body, kButtons[i]) + bh * 1.4f; total += bws[i]; }
    float bgap = W * 0.02f;
    float bx = (W - total - bgap * (kButtonCount - 1)) * 0.5f;
    for (int i = 0; i < kButtonCount; i++) {
        float an = launcher_anim_[N + i];
        Rect br = {bx, by, bws[i], bh};
        launcher_hit_[N + i] = br;
        video_.fill_round_rect(br, bh * 0.5f, lerp_color(0xFF1C2230, acc, an * 0.85f));
        video_.draw_text(FontId::Body, br.x + br.w * 0.5f, br.y + (bh - video_.line_height(FontId::Body)) * 0.5f, kButtons[i],
                         an > 0.5f ? 0xFF10131A : 0xFFD8DDE8, Align::Center);
        bx += bws[i] + bgap;
    }
#ifdef __ANDROID__
    const char* hint = touch_active_ ? "Tap a game to play    Add ROMs to choose your ROM files"
                                     : "\xE2\x86\x90\xE2\x86\x92 Choose    (A) Play    Back Quit";
#else
    const char* hint = "\xE2\x86\x90\xE2\x86\x92 Choose    Enter / (A) Play    F11 Fullscreen    Esc Quit";
#endif
    video_.draw_text(FontId::Small, W * 0.5f, H - video_.line_height(FontId::Small) * 1.6f, hint,
                     with_alpha(0xFF6C7488, 1.0f - menu_.toast_cover()), Align::Center);
}
