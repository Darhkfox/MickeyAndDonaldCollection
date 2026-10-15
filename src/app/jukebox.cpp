// Music player / sound test: a dedicated emulator instance runs the game's own sound driver.
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

bool Jukebox::open(GameModule* module, const std::vector<uint8_t>& rom, const Settings& s, bool sfx) {
    close();
    module_ = module;
    sfx_ = sfx;
    session_ = std::make_unique<Session>();
    session_->set_audio_rate(48000);
    if (!session_->open(module, rom)) { session_.reset(); return false; }
    Settings js = s;
    js.widescreen = WideMode::Off;
    js.cheats.clear();
    session_->apply_settings(js);
    module_->jukebox_boot(session_->md());
    list_.clear();
    for (const SoundEntry& e : module->sounds())
        if (sfx || e.music) list_.push_back(e);
    sel_ = 0;
    playing_ = -1;
    elapsed_ = 0;
    scroll_ = 0;
    free_scroll_ = false;
    paused_ = false;
    close_requested_ = false;
    switch_dir_ = 0;
    active_ = true;
    return true;
}

void Jukebox::close() {
    active_ = false;
    session_.reset();
    close_requested_ = false;
}

void Jukebox::update(const UiInput& in) {
    if (!active_) return;
    int n = (int)list_.size();
    if (in.menu || in.back) { close_requested_ = true; return; }
    if (n == 0) return;
    // touch: dragging scrolls the list with the finger, a tap on a track plays it (or stops it, if it is
    // playing), a tap on the game switch moves to the next game, a tap on the title area goes back
    UiInput t = in;
    if (in.drag != 0 && row_ > 0) {
        scroll_ -= in.drag / row_;
        free_scroll_ = true;
    }
    if (in.up || in.down || in.page_left || in.page_right) free_scroll_ = false;
    if (in.tap) {
        auto inside = [&](const Rect& r) { return in.tap_x >= r.x && in.tap_x < r.x + r.w && in.tap_y >= r.y && in.tap_y < r.y + r.h; };
        if (!sfx_ && inside(switch_rect_)) { switch_dir_ = 1; return; }
        if (in.tap_y < list_rect_.y) { close_requested_ = true; return; }
        if (inside(list_rect_) && row_ > 0) {
            int i = (int)std::floor((in.tap_y - list_y0_) / row_ + scroll_);
            if (i >= 0 && i < n) { sel_ = i; t.confirm = true; }
        }
    }
    const UiInput& inp = t;
    if (inp.up) sel_ = (sel_ - 1 + n) % n;
    if (inp.down) sel_ = (sel_ + 1) % n;
    if (inp.page_left) sel_ = std::max(0, sel_ - 10);
    if (inp.page_right) sel_ = std::min(n - 1, sel_ + 10);
    if (!sfx_ && (inp.left || inp.right)) switch_dir_ = inp.left ? -1 : 1;
    if (inp.confirm) {
        // Enter on the entry that is still playing stops it (music, and looping effects); a finished
        // effect has already gone back to "stopped", so Enter plays it again.
        if (playing_ == sel_) {
            module_->stop_sound(session_->md());
            playing_ = -1;
        } else {
            module_->play_sound(session_->md(), list_[sel_].id);   // a new track replaces the old one
            playing_ = sel_;
            elapsed_ = 0;
            silent_frames_ = 0;
        }
    }
}

void Jukebox::step() {
    if (!active_ || !session_) return;
    module_->jukebox_frame(session_->md());
    session_->run_frame(0, 0);
    if (playing_ >= 0) elapsed_ += 1.0 / 59.92;
    MegaDrive& md = session_->md();
    for (int i = 0; i < 10; i++) {
        float lv = i < 6 ? md.ym.channel_level[i] / 6000.0f : md.psg.channel_level[i - 6] / 2600.0f;
        lv = std::min(1.0f, std::sqrt(std::max(0.0f, lv)));
        vu_[i] = std::max(lv, vu_[i] * 0.86f);
        peak_[i] = std::max(vu_[i], peak_[i] - 0.012f);
    }
    // Music can open with seconds of silence, so only effects end on silence.
    if (playing_ >= 0 && !list_[playing_].music) {
        bool quiet = std::all_of(std::begin(vu_), std::end(vu_), [](float l) { return l < 0.02f; });
        silent_frames_ = quiet ? silent_frames_ + 1 : 0;
        if (silent_frames_ > 20) playing_ = -1;
    }
}

void Jukebox::mix_audio(std::vector<int16_t>& out, double adjust) {
    if (session_) session_->mix_audio(out, adjust);
}

void Jukebox::draw(Video& v, float time, uint32_t accent) {
    const float W = (float)v.width(), H = (float)v.height();
    v.fill_rect_gradient({0, 0, W, H}, lerp_color(0xFF0B0E18, accent, 0.10f), 0xFF07080D);
    const float pad = H * 0.05f;
    v.draw_text(FontId::Title, pad, pad * 0.7f, sfx_ ? "Sound Test" : "Music Player", accent);
    v.draw_text(FontId::Body, pad, pad * 0.7f + v.line_height(FontId::Title), module_->title(), 0xFFB9C1D2);
    if (!sfx_) {
        const char* sw = touch_ui ? "Tap here to switch game  \xE2\x96\xB6" : "\xE2\x97\x80 \xE2\x96\xB6  switch game";
        float tw = v.measure_text(FontId::Small, sw);
        v.draw_text(FontId::Small, W - pad, pad * 0.9f, sw, 0xFF7C8598, Align::Right);
        switch_rect_ = {W - pad - tw - pad * 0.5f, 0, tw + pad * 1.5f, pad * 0.9f + v.line_height(FontId::Small) * 1.6f};
    }

    // Track list
    Rect list = {pad, H * 0.2f, W * 0.46f, H * 0.68f};
    draw_panel(v, list, accent);
    const float row = v.line_height(FontId::Body) * 1.25f;
    list_rect_ = list; row_ = row; list_y0_ = list.y + pad * 0.3f;
    int visible = std::max(1, (int)((list.h - pad * 0.6f) / row));
    const float most = (float)std::max(0, (int)list_.size() - visible);
    float target = std::clamp(free_scroll_ ? scroll_ : (float)sel_ - visible * 0.5f + 0.5f, 0.0f, most);
    if (free_scroll_) scroll_ = target;
    scroll_ += (target - scroll_) * 0.3f;
    int first = std::max(0, (int)scroll_);
    for (int i = first; i < (int)list_.size() && i <= first + visible; i++) {
        float y = list.y + pad * 0.3f + (i - scroll_) * row;
        if (y < list.y + pad * 0.1f || y + row > list.y + list.h) continue;
        bool sel = i == sel_;
        if (sel) v.fill_round_rect({list.x + pad * 0.2f, y, list.w - pad * 0.4f, row * 0.94f}, row * 0.2f, with_alpha(accent, 0.28f));
        char num[16];
        snprintf(num, sizeof(num), "%02X", list_[i].id);
        uint32_t col = sel ? 0xFFFFFFFF : 0xFFC9D0DC;
        float ty = y + (row - v.line_height(FontId::Body)) * 0.4f;
        v.draw_text(FontId::Small, list.x + pad * 0.5f, ty + v.line_height(FontId::Body) * 0.12f, num, 0xFF7C8598);
        std::string name = list_[i].name;
        if (sfx_) name = std::string(list_[i].music ? "\xE2\x99\xAA " : "") + name;
        v.draw_text(FontId::Body, list.x + pad * 1.4f, ty, name, col);
        if (i == playing_) {
            float bounce = 0.5f + 0.5f * std::sin(time * 8.0f);
            v.draw_text(FontId::Body, list.x + list.w - pad * 0.5f, ty, "\xE2\x96\xB6", with_alpha(accent, 0.6f + 0.4f * bounce), Align::Right);
        }
    }

    // Now playing + meters
    Rect np = {W * 0.52f, H * 0.2f, W * 0.48f - pad, H * 0.68f};
    draw_panel(v, np, accent);
    float y = np.y + pad * 0.5f;
    v.draw_text(FontId::Small, np.x + pad * 0.6f, y, playing_ >= 0 ? "NOW PLAYING" : "STOPPED", 0xFF7C8598);
    y += v.line_height(FontId::Small) * 1.2f;
    std::string title = playing_ >= 0 ? list_[playing_].name : std::string("\xE2\x80\x94");
    float fit = std::min(1.0f, (np.w - pad * 1.2f) / std::max(1.0f, v.measure_text(FontId::Title, title)));
    v.draw_text(FontId::Title, np.x + pad * 0.6f, y, title, 0xFFFFFFFF, Align::Left, fit);
    y += v.line_height(FontId::Title) * 1.1f;
    int secs = playing_ >= 0 ? (int)elapsed_ : 0;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d:%02d", secs / 60, secs % 60);
    v.draw_text(FontId::Body, np.x + pad * 0.6f, y, tbuf, accent);
    y += v.line_height(FontId::Body) * 2.0f;

    static const char* labels[10] = {"FM1", "FM2", "FM3", "FM4", "FM5", "FM6", "SQ1", "SQ2", "SQ3", "NSE"};
    float mh = np.y + np.h - y - pad * 1.4f;
    float bw = (np.w - pad * 1.2f) / 10.0f;
    for (int i = 0; i < 10; i++) {
        float bx = np.x + pad * 0.6f + i * bw;
        Rect back = {bx + bw * 0.15f, y, bw * 0.7f, mh};
        v.fill_round_rect(back, bw * 0.12f, 0xFF1A1F2B);
        float h = mh * vu_[i];
        uint32_t c = i < 6 ? accent : lerp_color(accent, 0xFF6FCF8A, 0.7f);
        if (h > 1) v.fill_round_rect({back.x, back.y + mh - h, back.w, h}, bw * 0.12f, c);
        float py = back.y + mh - mh * peak_[i];
        v.fill_rect({back.x, py - 2, back.w, 3}, 0xE0FFFFFF);
        v.draw_text(FontId::Small, bx + bw * 0.5f, y + mh + pad * 0.2f, labels[i], 0xFF7C8598, Align::Center);
    }
    const char* help = sfx_ ? "\xE2\x86\x91\xE2\x86\x93 Select    Enter / (A) Play / Stop    PgUp/PgDn (LB/RB) Jump    Esc / (B) Back"
                            : "\xE2\x86\x91\xE2\x86\x93 Select    Enter / (A) Play / Stop    \xE2\x86\x90\xE2\x86\x92 Switch game    Esc / (B) Back";
    if (touch_ui) help = "Tap a track to play or stop it    Drag to scroll    Back to return";
    v.draw_text(FontId::Small, W * 0.5f, H - v.line_height(FontId::Small) * 1.8f, help, with_alpha(0xFF6C7488, 1.0f - hint_fade_), Align::Center);
}
