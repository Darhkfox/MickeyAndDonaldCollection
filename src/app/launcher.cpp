// Launcher: game cards with live attract-mode previews, plus Options / Music Player / Credits / Quit.
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "logo_data.h"
#include "imageload.h"

namespace {
std::string narrow(const std::wstring& w) { return to_utf8(w); }
const int kButtonCount = 5;
#ifdef __ANDROID__
// Android apps don't quit from a menu (Back on the launcher still offers it); ROMs come in through the picker.
const char* kButtons[kButtonCount] = {"Options", "Achievements", "Music Player", "Credits", "Add ROMs"};
#else
const char* kButtons[kButtonCount] = {"Options", "Achievements", "Music Player", "Credits", "Quit"};
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

const char* kPatreonShown = "patreon.com/TeamRedTail";
const char* kPatreonUrl = "https://www.patreon.com/TeamRedTail";

}

bool App::over_launcher_link(float x, float y) const {
    const Rect& r = launcher_link_;
    return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void App::open_patreon() {
    if (autotest_) return;   // scripted test runs never open a browser
    open_url(kPatreonUrl);
    menu_.toast("Opened Patreon in your browser");
}

// ---------------------------------------------------------------------------
// Updates: once a day (or on Check Now) the latest release's tag is looked up on GitHub; a newer one gets a badge
// on the launcher (and Options a link) that opens its page. Nothing is downloaded or installed.
// ---------------------------------------------------------------------------
std::string App::update_version() const {
    return update_tag_.size() > 1 && (update_tag_[0] == 'v' || update_tag_[0] == 'V') ? update_tag_.substr(1) : update_tag_;
}

bool App::over_update_badge(float x, float y) const {
    const Rect& r = launcher_update_;
    return !update_tag_.empty() && r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void App::open_update_page() {
    if (update_tag_.empty() || autotest_) return;
    open_url(UpdateCheck::page_for(update_tag_).c_str());
    menu_.toast("Opened the version " + update_version() + " page in your browser");
}

void App::check_for_update(bool manual) {
    if (autotest_) {   // (scripted runs: no network, only what the settings say an earlier check found)
        apply_update_tag(settings_.check_updates ? settings_.update_latest : std::string());
        return;
    }
    if (!manual && !settings_.check_updates) return;
    if (!manual && settings_.update_checked_on == today_text()) {
        apply_update_tag(settings_.update_latest);   // (checked today already)
        return;
    }
    update_manual_ = manual;
    updater_.start();
}

void App::apply_update_tag(const std::string& tag) {
    const bool newer = !tag.empty() && UpdateCheck::is_newer(tag, kAppVersion);
    update_tag_ = newer && (settings_.check_updates || update_manual_) ? tag : std::string();
    if (!update_tag_.empty() && !update_told_) {
        update_told_ = true;
        menu_.toast("Version " + update_version() + " is available - see the badge, or Options > Updates", 6.0f);
    }
}

void App::update_tick() {
    if (!update_started_) {
        update_started_ = true;
        check_for_update(false);
    }
    std::string tag;
    if (!updater_.poll(&tag)) return;
    const bool manual = update_manual_;
    if (!tag.empty()) {
        settings_.update_checked_on = today_text();
        settings_.update_latest = tag;
        save_settings();
        if (manual) update_told_ = false;
        apply_update_tag(tag);
        if (manual && update_tag_.empty()) menu_.toast(std::string("You have the latest version (") + kAppVersion + ")", 4.0f);
    } else if (manual) {
        menu_.toast("Couldn't reach GitHub to check for updates", 4.0f);
    }
    update_manual_ = false;
}

void App::update_launcher(const UiInput& ui, double dt) {
    const int N = kGameCount;          // cards 0..N-1, buttons N..N+4
    poll_roms(dt);
    int& s = launcher_sel_;
    if (s < N) launcher_last_card_ = s;
    if (ui.left && s != 0 && s != N) s--;
    if (ui.right && s != N - 1 && s != N + kButtonCount - 1) s++;
    if (ui.down && s < N) s = N + 2;
    if (ui.up && s >= N) s = launcher_last_card_;
    bool go = ui.confirm;
    if (ui.tap && over_launcher_link(ui.tap_x, ui.tap_y)) open_patreon();
    else if (ui.tap && over_update_badge(ui.tap_x, ui.tap_y)) open_update_page();
    else if (ui.tap)   // touch: a card or button is chosen and used at once
        for (int i = 0; i < N + kButtonCount; i++) {
            const Rect& r = launcher_hit_[i];
            if (ui.tap_x >= r.x && ui.tap_x < r.x + r.w && ui.tap_y >= r.y && ui.tap_y < r.y + r.h) { s = i; go = true; }
        }
    if (s < N) launcher_last_card_ = s;
    if (go) {
        if (s < N) launch(s);
        else if (s == N) open_options_menu(true);
        else if (s == N + 1) menu_.open(achievements_page(-1));
        else if (s == N + 2) {
            int g = -1;
            for (int k = 0; k < N && g < 0; k++) {
                int c = (launcher_last_card_ + k) % N;
                if (games_[c].rom.found) g = c;
            }
            if (g >= 0) open_jukebox(g, false);
            else menu_.toast("No ROMs found");
        } else if (s == N + 3) open_credits(true);
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

// ---------------------------------------------------------------------------
// The player's pictures: covers (covers/<game id>) and the background (covers/background), PNG or JPEG
// ---------------------------------------------------------------------------
std::wstring App::covers_dir() const { return path_join(data_dir(), L"covers"); }

bool App::has_cover(int g) const {
    return art_covers_ && g >= 0 && g < kGameCount && cover_art_[g].w > 0;
}

// Reads the pictures the settings ask for (again if `reload`, or when the settings changed), and starts or
// stops the attract-mode previews to match: a card with a cover has none (lighter on slow devices).
void App::load_launcher_art(bool reload) {
    const bool covers = settings_.launcher_covers, bg = settings_.launcher_background;
    if (!reload && art_loaded_ && covers == art_covers_ && bg == art_bg_) return;
    art_loaded_ = true;
    auto drop = [this](Art& a) {
        if (a.image) video_.free_image(a.image);
        a = Art();
    };
    auto read = [&](Art& a, const std::string& name, int max_side) {
        drop(a);
        for (const wchar_t* ext : {L".png", L".jpg", L".jpeg", L".PNG", L".JPG", L".JPEG"}) {
            const std::wstring path = path_join(covers_dir(), from_utf8(name) + ext);
            if (load_image_file(path, max_side, a.px, &a.w, &a.h)) return;
        }
        a = Art();
    };
    art_covers_ = covers;
    art_bg_ = bg;
    for (int g = 0; g < kGameCount; g++) {
        if (covers) read(cover_art_[g], games_[g].module->id(), 1024);
        else drop(cover_art_[g]);
    }
    if (bg) read(bg_art_, "background", 2048);
    else drop(bg_art_);
    ensure_previews();
}

int App::art_image(Art& a) {
    if (!a.image && a.w > 0) a.image = video_.create_image(a.px.data(), a.w, a.h);
    return a.image;
}

namespace {
// A small hash for the stars' places (the same every frame).
float hash01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (n & 0xFFFFFF) / 16777216.0f;
}
}

// The Theatre style's launcher backdrop: a starry night sky with a moon, red velvet curtains either side
// (`side` px wide: the room beside the cards) and a wooden stage under the buttons (from `floor_y`). The moon only
// where it's clear of the title (ending at `title_right`).
void App::draw_stage(float W, float H, float side, float floor_y, float title_right) {
    const float t = (float)time_;
    video_.fill_rect_gradient({0, 0, W, floor_y * 0.55f}, 0xFF060820, 0xFF141640);
    video_.fill_rect_gradient({0, floor_y * 0.55f, W, floor_y * 0.45f + 1}, 0xFF141640, 0xFF2C1C52);
    // stars: placed by a hash, twinkling; fewer near the stage, where the footlights glow
    const int n = std::clamp((int)(W * H / 9000.0f), 40, 260);
    for (int i = 0; i < n; i++) {
        const float x = hash01(i * 3 + 1) * W, y = hash01(i * 3 + 2) * floor_y * 0.92f;
        const float big = hash01(i * 3 + 3);
        const float r = H * (0.0016f + 0.0042f * big * big);
        const float tw = 0.55f + 0.45f * std::sin(t * (1.1f + 2.3f * hash01(i * 7 + 5)) + i * 1.7f);
        const float a = (0.35f + 0.65f * tw) * (1.0f - 0.7f * y / floor_y);
        if (r > H * 0.0035f) video_.fill_star(x, y, r * 1.6f, with_alpha(0xFFFFF2C8, a));
        else video_.fill_round_rect({x - r, y - r, r * 2, r * 2}, r, with_alpha(0xFFFFF6DC, a));
    }
    // the moon, high on the right with a soft halo
    const float mr = H * 0.05f, mx = W - std::max(side, H * 0.02f) - mr * 2.2f, my = H * 0.135f;
    if (mx - mr * 1.8f > title_right) {
        for (int k = 4; k >= 1; k--) {
            const float g = mr * (1.0f + 0.45f * k);
            video_.fill_round_rect({mx - g, my - g, g * 2, g * 2}, g, with_alpha(0xFFFFE7A8, 0.045f));
        }
        video_.fill_round_rect({mx - mr, my - mr, mr * 2, mr * 2}, mr, 0xFFF7E9BE);
        const float cr[3][3] = {{-0.3f, -0.25f, 0.22f}, {0.32f, 0.1f, 0.15f}, {-0.05f, 0.38f, 0.12f}};
        for (const auto& c : cr)
            video_.fill_round_rect({mx + c[0] * mr - c[2] * mr, my + c[1] * mr - c[2] * mr, c[2] * mr * 2, c[2] * mr * 2},
                                   c[2] * mr, 0x30A08850);
    }
    // the footlights' warm glow on the backdrop, then the stage: boards, its front edge, the shade below
    video_.fill_rect_gradient({0, floor_y - H * 0.16f, W, H * 0.16f}, 0x00FFB050, 0x38FFB050);
    video_.fill_rect_gradient({0, floor_y, W, H - floor_y}, 0xFF5A3418, 0xFF1C0F07);
    const float lw = std::max(1.0f, std::round(H / 540.0f));
    for (float y = floor_y + H * 0.03f; y < H; y += H * 0.032f) video_.fill_rect({0, y, W, lw}, 0x40000000);
    video_.fill_rect({0, floor_y, W, std::max(2.0f, H / 260.0f)}, 0xFFB07A3C);
    // curtains: velvet folds (a shade per narrow strip), a gold trim along the inner edges
    if (side >= 2.0f) {
        const float step = std::max(1.0f, std::round(H / 400.0f)), fold = std::max(H * 0.045f, side / 3.0f);
        for (int s = 0; s < 2; s++)
            for (float x = 0; x < side; x += step) {
                const float ph = x / fold * 3.14159265f;
                const float lit = 0.35f + 0.65f * std::pow(std::fabs(std::cos(ph)), 1.6f);
                const uint32_t c = lerp_color(0xFF3A0610, 0xFFB01C2C, lit);
                const float px = s == 0 ? x : W - x - step;
                video_.fill_rect_gradient({px, 0, step, H}, lerp_color(c, 0xFF000000, 0.45f), lerp_color(c, 0xFF000000, 0.2f));
            }
        const float tw = std::max(2.0f, H / 300.0f);
        video_.fill_rect({side - tw, 0, tw, H}, kGoldDeep);
        video_.fill_rect({W - side, 0, tw, H}, kGoldDeep);
    }
}

void App::draw_launcher() {
    const float W = (float)video_.width(), H = (float)video_.height();
    const int N = kGameCount;
    uint32_t acc = accent();
    const bool th = ui_style() == kStyleTheatre;
    const bool custom_bg = art_bg_ && bg_art_.w > 0;
    // (over the player's own picture, text gets a dark outline: it reads over anything)
    auto label = [&](FontId f, float x, float y, const std::string& s, uint32_t c, Align al, float sc = 1.0f) {
        if (custom_bg) draw_text_outlined(video_, f, x, y, s, c, 0xD0000008, al, sc);
        else video_.draw_text(f, x, y, s, c, al, sc);
    };

    // ---- layout ----
    // Title (narrower where it would leave the logo no room beside it)
    float ty0 = H * 0.035f;
    const float logo_m = H * 0.03f, title_max = std::min(W * 0.86f, W - 2 * (H * 0.16f + logo_m * 2));
    const float title_w = std::max(1.0f, video_.measure_text(FontId::Huge, "Mickey & Donald Collection"));
    float tscale = std::min(0.8f, std::max(W * 0.6f, title_max) / title_w);
    tscale = std::min(tscale, W * 0.86f / title_w);
    const float title_y = ty0;
    ty0 += video_.line_height(FontId::Huge, tscale);
    const float sub_y = ty0;
    ty0 += video_.line_height(FontId::Body);
    // Cards: the frames are the game's screen's shape, or square when there are covers (box art fills them better)
    bool any_cover = false;
    for (int i = 0; i < N; i++) any_cover = any_cover || (has_cover(i) && games_[i].rom.found);
    const float aspect = any_cover ? 1.0f : 320.0f / 224.0f;
    const float top = ty0 + H * 0.045f;
    float gap = W * 0.03f;
    // The text under the preview is sized by the window height (fonts), the preview by the card width: give the
    // text the room its lines need and fit the card width to what is left (narrow 4:3 windows).
    const float rad0 = H * 0.02f;
    const float info_h = rad0 * 0.7f + video_.line_height(FontId::Title) * 0.95f +
                         video_.line_height(FontId::Small) * (1.05f + 1.1f + 1.0f) + rad0 * 0.6f;
    float cw = std::min((W * 0.94f - gap * (N - 1)) / N, (H * 0.82f - top - info_h - rad0 * 0.6f) * aspect + rad0 * 1.2f);
    float ph = rad0 * 0.6f + (cw - rad0 * 1.2f) / aspect;
    float ch = ph + info_h;
    float x0 = (W - (cw * N + gap * (N - 1))) * 0.5f;
    const float by = H * 0.855f;
    const float card_top = top + std::max(0.0f, (by - H * 0.03f - top - ch) * 0.5f);   // (centred above the buttons)

    // ---- background ----
    if (custom_bg && art_image(bg_art_)) {
        // the player's picture filling the window (its middle; not stretched), darkened a little and shaded at the
        // top and the bottom, where the title and the buttons are
        const float s = std::max(W / bg_art_.w, H / bg_art_.h), vw = W / s, vh = H / s;
        video_.fill_rect({0, 0, W, H}, 0xFF000000);
        video_.draw_image(bg_art_.image, {0, 0, W, H}, {(bg_art_.w - vw) * 0.5f, (bg_art_.h - vh) * 0.5f, vw, vh});
        video_.fill_rect({0, 0, W, H}, 0x48000008);
        video_.fill_rect_gradient({0, 0, W, H * 0.25f}, 0xA0000008, 0x00000008);
        video_.fill_rect_gradient({0, H * 0.72f, W, H * 0.28f}, 0x00000008, 0xB0000008);
    } else if (th) {
        draw_stage(W, H, std::clamp(x0 - H * 0.015f, 0.0f, W * 0.16f), H * 0.835f,
                   update_tag_.empty() ? (W + title_w * tscale) * 0.5f : W);   // (no moon beside an update badge)
    } else {
        // Classic: black with a fine slate grid, centred so it stays symmetric at any window size.
        video_.fill_rect({0, 0, W, H}, 0xFF060708);
        const float cell = std::round(H / 14.0f), line = std::max(1.0f, std::round(H / 336.0f));
        const uint32_t grid = 0xFF333B45;
        for (float x = std::fmod(W * 0.5f, cell) - line * 0.5f; x < W; x += cell) video_.fill_rect({x, 0, line, H}, grid);
        for (float y = std::fmod(H * 0.5f, cell) - line * 0.5f; y < H; y += cell) video_.fill_rect({0, y, W, line}, grid);
    }

    // ---- title, logo, subtitle ----
    if (th) {
        const float o = std::max(2.0f, H / 260.0f);
        video_.draw_text(FontId::Huge, W * 0.5f + o * 1.2f, title_y + o * 2.2f, "Mickey & Donald Collection", 0xB0000000,
                         Align::Center, tscale);
        draw_text_outlined(video_, FontId::Huge, W * 0.5f, title_y, "Mickey & Donald Collection", kGold, kInk, Align::Center,
                           tscale, o);
    } else {
        label(FontId::Huge, W * 0.5f, title_y, "Mickey & Donald Collection", 0xFFF6F1E4, Align::Center, tscale);
    }
    // The Team RedTail logo, top left, the Patreon page under it: smaller where the title leaves less room
    // beside it (none: not shown).
    {
        const float m = logo_m, room = (W - title_w * tscale) * 0.5f - m * 2;
        const float lw = std::round(std::min(room, H * 0.13f * kLogoW / kLogoH)), lh = std::round(lw * kLogoH / kLogoW);
        launcher_link_ = {};
        if (lw > H * 0.06f && lh > 0) {
            if (!logo_image_) {
                std::vector<uint32_t> px;
                px.reserve((size_t)kLogoW * kLogoH);
                for (size_t k = 0; k + 1 < sizeof(kLogoRle); k += 2) px.insert(px.end(), kLogoRle[k], kLogoPal[kLogoRle[k + 1]]);
                px.resize((size_t)kLogoW * kLogoH);
                logo_image_ = video_.create_image(px.data(), kLogoW, kLogoH);
            }
            const float fw = video_.measure_text(FontId::Small, kPatreonShown);
            const float fs = std::min(1.0f, std::min(lw * 1.7f, room) / std::max(1.0f, fw));
            const float bw = std::max(lw, fw * fs);   // (the two centred on each other)
            const float lx = std::round(m + (bw - lw) * 0.5f), ly = std::round(m);
            video_.draw_image(logo_image_, {lx, ly, lw, lh});
            const float py = ly + lh + H * 0.004f;
            label(FontId::Small, m + bw * 0.5f, py, kPatreonShown, th ? 0xFFC3B9E2 : 0xFF9AA4BA, Align::Center, fs);
            launcher_link_ = {m, m, bw, py + video_.line_height(FontId::Small, fs) - m};
        }
    }
#ifdef __ANDROID__
    const char* edition = "Android Edition";
#else
    const char* edition = "PC Edition";
#endif
    const std::string subtitle = std::string("Sega Mega Drive / Genesis classics  \xE2\x80\xA2  ") + edition + "  \xE2\x80\xA2  v" + kAppVersion;
    // A newer release: a badge on the right (it opens its page), beside the title or else beside the line under it;
    // where neither has room, a shorter one under the Patreon link.
    launcher_update_ = {};
    if (!update_tag_.empty()) {
        const float bh = video_.line_height(FontId::Small) * 1.45f, m = logo_m;
        std::string t = "Update available: v" + update_version();
        float bw = video_.measure_text(FontId::Small, t) + bh * 1.7f;
        const float sub_right = (W + video_.measure_text(FontId::Body, subtitle)) * 0.5f;
        Rect r;
        if (W - m - bw >= (W + title_w * tscale) * 0.5f + m) {
            r = {W - m - bw, H * 0.075f, bw, bh};
        } else if (W - m - bw >= sub_right + m) {
            r = {W - m - bw, sub_y + (video_.line_height(FontId::Body) - bh) * 0.5f, bw, bh};
        } else {
            t = "v" + update_version() + " available";
            bw = video_.measure_text(FontId::Small, t) + bh * 1.7f;
            const float y = launcher_link_.w > 0 ? launcher_link_.y + launcher_link_.h + H * 0.012f : m;
            r = {m, y, bw, bh};
        }
        const float pulse = 0.5f + 0.5f * std::sin((float)time_ * 3.0f);
        video_.fill_round_rect({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, bh * 0.5f + 3, with_alpha(0xFFFFD84A, 0.25f + 0.25f * pulse));
        video_.fill_round_rect(r, bh * 0.5f, 0xFFFFD040);
        video_.outline_round_rect(r, bh * 0.5f, std::max(2.0f, H / 360.0f), kInk);
        video_.fill_star(r.x + bh * 0.55f, r.y + bh * 0.5f, bh * 0.3f, kInk);
        video_.fill_star(r.x + bh * 0.55f, r.y + bh * 0.5f, bh * 0.2f, 0xFFFFFFFF);
        video_.draw_text(FontId::Small, r.x + bh * 1.0f + (r.w - bh * 1.5f) * 0.5f, r.y + (bh - video_.line_height(FontId::Small)) * 0.5f, t,
                         kInk, Align::Center);
        launcher_update_ = r;
    }
    label(FontId::Body, W * 0.5f, sub_y, subtitle, th ? 0xFFCFC6EA : 0xFF9AA4BA, Align::Center);

    // ---- cards ----
    // ROM file names share one text size (the one the longest name needs)
    std::string rom_label[kGameCount];
    float rom_fs = 1.0f;
    for (int i = 0; i < N; i++) {
        if (!games_[i].rom.found) continue;
        const std::wstring& path = games_[i].rom.path;
        rom_label[i] = "\xE2\x9C\x93 " + narrow(path.substr(path.find_last_of(L"\\/") + 1));
        if (games_[i].roms.size() > 1) {   // two releases: which are there (Play asks which to start when both are)
            const std::vector<RomVariant> variants = games_[i].module->rom_variants();
            std::string which;
            for (size_t v = 0; v < games_[i].roms.size(); v++)
                if (games_[i].roms[v].found) which += (which.empty() ? "" : " + ") + variants[v].label;
            rom_label[i] = "\xE2\x9C\x93 " + which + (games_[i].found_count() > 1 ? "" : " - " + rom_label[i].substr(4));
        }
        rom_fs = std::min(rom_fs, cw * 0.92f / std::max(1.0f, video_.measure_text(FontId::Small, rom_label[i])));
    }
    for (int i = 0; i < N; i++) {
        GameSlot& g = games_[i];
        float an = launcher_anim_[i];
        float sc = 1.0f + 0.035f * an;
        float cx = x0 + i * (cw + gap) + cw * 0.5f, cy = card_top + ch * 0.5f;
        Rect r = {cx - cw * sc * 0.5f, cy - ch * sc * 0.5f, cw * sc, ch * sc};
        launcher_hit_[i] = r;
        uint32_t c = g.module->accent_color();
        float rad = H * 0.02f;
        if (an > 0.01f) {
            float glow = H * 0.012f * an;
            video_.fill_round_rect({r.x - glow, r.y - glow, r.w + glow * 2, r.h + glow * 2}, rad + glow, with_alpha(c, 0.25f * an));
        }
        video_.fill_round_rect(r, rad, th ? 0xF4181238 : 0xFF141824);
        Rect pr = {r.x + rad * 0.6f, r.y + rad * 0.6f, r.w - rad * 1.2f, (r.w - rad * 1.2f) / aspect};
        if (has_cover(i) && g.rom.found && art_image(cover_art_[i])) {
            // (until its ROM is found, a card says what's missing instead)
            // The player's cover, whole (the dark backdrop either side) or filling its frame (trimmed; a tall
            // cover keeps more of its top, where box art has its logo); dimmed when not chosen.
            Art& a = cover_art_[i];
            video_.fill_round_rect(pr, rad * 0.5f, th ? 0xFF0A0820 : 0xFF0A0D14);
            if (settings_.cover_fill) {
                const float s = std::max(pr.w / a.w, pr.h / a.h), vw = pr.w / s, vh = pr.h / s;
                video_.draw_image(a.image, pr, {(a.w - vw) * 0.5f, (a.h - vh) * 0.25f, vw, vh});
            } else {
                const float s = std::min(pr.w / a.w, pr.h / a.h), dw = a.w * s, dh = a.h * s;
                video_.draw_image(a.image, {pr.x + (pr.w - dw) * 0.5f, pr.y + (pr.h - dh) * 0.5f, dw, dh});
            }
            if (an < 0.99f) video_.fill_rect(pr, with_alpha(0xFF000000, 0.35f * (1.0f - an)));
        } else if (g.preview) {
            // (in a square frame beside covers: the picture its own shape, centred)
            Rect ip = pr;
            if (any_cover) {
                video_.fill_round_rect(pr, rad * 0.5f, th ? 0xFF0A0820 : 0xFF0A0D14);
                ip.h = pr.w * 224.0f / 320.0f;
                ip.y = pr.y + (pr.h - ip.h) * 0.5f;
            }
            draw_game_view(1 + i, *g.preview, ip, 1.0f, 0.35f * (1.0f - an));
        } else {
            video_.fill_round_rect(pr, rad * 0.5f, th ? 0xFF0A0820 : 0xFF0A0D14);
            // What is missing (or which file is the wrong version), shrunk to fit the preview area.
            const bool other = !g.rom.other_file.empty();
            const std::string head = other ? "Wrong ROM version" : "ROM not found";
            char crc[96];
            const std::vector<RomVariant> variants = g.module->rom_variants();
            if (variants.size() > 1)   // (the sizes differ: the CRCs say which is which)
                snprintf(crc, sizeof(crc), "(CRC32 %08X or %08X%s).", variants[0].crc, variants[1].crc, other ? "" : "; zipped is fine");
            else
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
            float tht = video_.line_height(FontId::Title, hs) * 1.15f + video_.line_height(FontId::Small, bs) * lines.size();
            const float vs = std::min(1.0f, pr.h * 0.9f / tht);
            hs *= vs;
            bs *= vs;
            float y = pr.y + (pr.h - tht * vs) * 0.5f;
            video_.draw_text(FontId::Title, pr.x + pr.w * 0.5f, y, head, other ? 0xFFF0B060 : 0xFFE06A6A, Align::Center, hs);
            y += video_.line_height(FontId::Title, hs) * 1.15f;
            for (const auto& l : lines) {
                video_.draw_text(FontId::Small, pr.x + pr.w * 0.5f, y, l, 0xFFB0B8C8, Align::Center, bs);
                y += video_.line_height(FontId::Small, bs);
            }
        }
        float ty = pr.y + pr.h + rad * 0.7f;
        float tfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Title, g.module->title())));
        video_.draw_text(FontId::Title, r.x + r.w * 0.5f, ty, g.module->title(), an > 0.5f ? c : (th ? kCream : 0xFFE6E9F0),
                         Align::Center, tfs);
        ty += video_.line_height(FontId::Title, tfs) * 0.95f;
        std::string sub = g.module->subtitle();
        float sfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Small, sub)));
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, sub, th ? 0xFFC0B7DE : 0xFF9AA4BA, Align::Center, sfs);
        ty += video_.line_height(FontId::Small) * 1.05f;
        std::string meta = std::string(g.module->year()) + "  \xE2\x80\xA2  " +
                           (g.module->max_players() > 1 ? "1-2 Players" : "1 Player");
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, meta,
                         g.module->max_players() > 1 ? lerp_color(c, 0xFFFFFFFF, 0.3f) : (th ? 0xFF9C93C0 : 0xFF7C8598), Align::Center);
        ty += video_.line_height(FontId::Small) * 1.1f;
        if (g.rom.found) video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, rom_label[i], 0xFF6FCF8A, Align::Center, rom_fs);
        if (th)   // gold trim: bright on the chosen card
            video_.outline_round_rect(r, rad, std::max(1.5f, H / 400.0f) * (0.7f + an), with_alpha(kGold, 0.4f + 0.6f * an));
        else
            video_.outline_round_rect(r, rad, std::max(1.5f, H / 400.0f) * (0.5f + an), with_alpha(an > 0.01f ? c : 0xFF2A3040, 0.6f + 0.4f * an));
    }

    // ---- buttons ----
    float bh = video_.line_height(FontId::Body) * 1.6f;
    float bws[kButtonCount], total = 0;
    for (int i = 0; i < kButtonCount; i++) { bws[i] = video_.measure_text(FontId::Body, kButtons[i]) + bh * 1.4f; total += bws[i]; }
    float bgap = W * 0.02f;
    // the row shrinks to fit narrow (4:3, portrait) windows
    const float bs = std::min(1.0f, W * 0.96f / (total + bgap * (kButtonCount - 1)));
    for (float& w : bws) w *= bs;
    total *= bs;
    bgap *= bs;
    float bx = (W - total - bgap * (kButtonCount - 1)) * 0.5f;
    for (int i = 0; i < kButtonCount; i++) {
        float an = launcher_anim_[N + i];
        Rect br = {bx, by + bh * (1.0f - bs) * 0.5f, bws[i], bh * bs};
        launcher_hit_[N + i] = br;
        if (th) {   // velvet plaques with gold trim; the chosen one lit gold
            video_.fill_round_rect({br.x, br.y + br.h * 0.08f, br.w, br.h}, br.h * 0.5f, 0x80000000);
            video_.fill_round_rect(br, br.h * 0.5f, lerp_color(0xF2241C4C, kGold, an));
            video_.outline_round_rect(br, br.h * 0.5f, std::max(1.0f, H / 540.0f), with_alpha(kGold, 0.55f + 0.45f * an));
        } else {
            video_.fill_round_rect(br, br.h * 0.5f, lerp_color(0xFF1C2230, acc, an * 0.85f));
        }
        video_.draw_text(FontId::Body, br.x + br.w * 0.5f, br.y + (br.h - video_.line_height(FontId::Body, bs)) * 0.5f, kButtons[i],
                         an > 0.5f ? (th ? kInk : 0xFF10131A) : (th ? kCream : 0xFFD8DDE8), Align::Center, bs);
        bx += bws[i] + bgap;
    }
#ifdef __ANDROID__
    const char* hint = touch_active_ ? "Tap a game to play    Add ROMs to choose your ROM files"
                                     : "\xE2\x86\x90\xE2\x86\x92 Choose    (A) Play    Back Quit";
#else
    const char* hint = "\xE2\x86\x90\xE2\x86\x92 Choose    Enter / (A) Play    F11 Fullscreen    Esc Quit";
#endif
    label(FontId::Small, W * 0.5f, H - video_.line_height(FontId::Small) * 1.6f, hint,
          with_alpha(th ? 0xFFD2BC98 : 0xFF6C7488, 1.0f - menu_.toast_cover()), Align::Center);
}
