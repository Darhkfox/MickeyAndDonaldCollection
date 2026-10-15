#include "app.h"
#include "png.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <sstream>
#ifdef __ANDROID__
#include <SDL.h>
#endif

// The platform-independent part of the app: frame loop body, input handling, game control, saves.
// Window, main loop and test recording: app_win.cpp (Windows), app_android.cpp (Android).

static Settings preview_settings(const Settings& s) {
    Settings p = s;
    p.widescreen = WideMode::Off;
    p.cheats.clear();
    p.volume = s.volume * 55 / 100;
    return p;
}

void App::apply_settings() {
    video_.set_vsync(settings_.vsync);
    if (session_) session_->apply_settings(settings_);
    for (auto& g : games_)
        if (g.preview) g.preview->apply_settings(preview_settings(settings_));
    if (jukebox_.active()) jukebox_.apply_settings(settings_);
    if (is_fullscreen_ != settings_.fullscreen) set_fullscreen(settings_.fullscreen);
}

void App::save_settings() { if (!autotest_) settings_.save(settings_path_); }

uint32_t App::accent() const {
    if (mode_ == Mode::Playing && current_ >= 0) return games_[current_].module->accent_color();
    return launcher_sel_ < kGameCount ? games_[launcher_sel_].module->accent_color() : 0xFFE8B84A;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
UiInput App::poll_ui(float dt) {
    UiInput u;
    bool dir[4] = {input_.key_down(VK_UP), input_.key_down(VK_DOWN), input_.key_down(VK_LEFT), input_.key_down(VK_RIGHT)};
    // Pads are read globally (XInput / background DirectInput): an autotest run uses only its script.
    for (int i = 0; i < Input::kMaxPads && !autotest_; i++) {
        const PadState& p = input_.pad(i);
        if (!p.connected) continue;
        if (p.buttons & (PAD_UP | PAD_LS_UP)) dir[0] = true;
        if (p.buttons & (PAD_DOWN | PAD_LS_DOWN)) dir[1] = true;
        if (p.buttons & (PAD_LEFT | PAD_LS_LEFT)) dir[2] = true;
        if (p.buttons & (PAD_RIGHT | PAD_LS_RIGHT)) dir[3] = true;
        if (p.pressed & PAD_A) u.confirm = true;
        if (p.pressed & PAD_B) u.back = true;
        if (p.pressed & (PAD_BACK | PAD_GUIDE)) u.menu = true;
        if (p.pressed & PAD_LB) u.page_left = true;
        if (p.pressed & PAD_RB) u.page_right = true;
        u.pad_pressed |= p.pressed;
    }
    bool* outs[4] = {&u.up, &u.down, &u.left, &u.right};
    for (int i = 0; i < 4; i++) {
        if (dir[i]) {
            if (!rep_held_[i]) { *outs[i] = true; rep_t_[i] = 0.36f; }
            else {
                rep_t_[i] -= dt;
                if (rep_t_[i] <= 0) { *outs[i] = true; rep_t_[i] += 0.07f; }
            }
        }
        rep_held_[i] = dir[i];
    }
    bool alt = input_.key_down(VK_MENU);
    if (!alt && (input_.key_pressed(VK_RETURN) || input_.key_pressed(VK_SPACE))) u.confirm = true;
    if (input_.key_pressed(VK_ESCAPE)) u.menu = true;
    if (input_.key_pressed(VK_PRIOR)) u.page_left = true;
    if (input_.key_pressed(VK_NEXT)) u.page_right = true;
    u.key_pressed = input_.last_key_pressed();
#ifdef __ANDROID__
    if (touch_.take_tap(u.tap_x, u.tap_y)) u.tap = true;
    u.drag = touch_.take_drag();
    if (touch_.take_pressed(TouchControls::BtnMenu)) u.menu = true;
#endif
    return u;
}

uint16_t App::pad_state(int p) {
    static const uint16_t bits[ACT_COUNT] = {MD_UP, MD_DOWN, MD_LEFT, MD_RIGHT, MD_A, MD_B, MD_C, MD_START};
    uint16_t s = 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (settings_.keys[p][a] && input_.key_down(settings_.keys[p][a])) s |= bits[a];
    if (autotest_) return s;
    bool connected[Input::kMaxPads];
    for (int i = 0; i < Input::kMaxPads; i++) connected[i] = input_.pad(i).connected;
    const int pi = resolve_player_pad(settings_.pad_index, connected, Input::kMaxPads, p);
    if (pi >= 0 && input_.pad(pi).connected) {
        const PadState& ps = input_.pad(pi);
        for (int a = 0; a < ACT_COUNT; a++)
            if (ps.buttons & settings_.pad_buttons[p][a]) s |= bits[a];
        if (settings_.stick_as_dpad) {
            if (ps.buttons & PAD_LS_UP) s |= MD_UP;
            if (ps.buttons & PAD_LS_DOWN) s |= MD_DOWN;
            if (ps.buttons & PAD_LS_LEFT) s |= MD_LEFT;
            if (ps.buttons & PAD_LS_RIGHT) s |= MD_RIGHT;
        }
    }
#ifdef __ANDROID__
    if (p == 0) s |= touch_.md_buttons();
#endif
    if ((s & (MD_UP | MD_DOWN)) == (MD_UP | MD_DOWN)) s &= ~(MD_UP | MD_DOWN);
    if ((s & (MD_LEFT | MD_RIGHT)) == (MD_LEFT | MD_RIGHT)) s &= ~(MD_LEFT | MD_RIGHT);
    return s;
}

bool App::any_game_button_held() { return pad_state(0) != 0 || pad_state(1) != 0; }

// A hold-to-use hotkey: its key or pad button on any controller, unless a player has bound it to a game button.
bool App::hold_hotkey(int vk, uint32_t pad_button) {
    bool key_free = true, pad_free = true;
    for (int p = 0; p < 2; p++)
        for (int a = 0; a < ACT_COUNT; a++) {
            if (settings_.keys[p][a] == vk) key_free = false;
            if (settings_.pad_buttons[p][a] & pad_button) pad_free = false;
        }
    if (key_free && input_.key_down(vk)) return true;
    if (autotest_ || !pad_free) return false;
    for (int i = 0; i < Input::kMaxPads; i++)
        if (input_.pad(i).connected && (input_.pad(i).buttons & pad_button)) return true;
    return false;
}

// Steps back at twice normal speed in real time: 60 saved states (2 frames apart) per second whatever the display's
// refresh rate, loading only the last one reached in this displayed frame. The frame after it is run silently to
// have a picture; at the oldest state the picture holds until the button is released.
void App::rewind_step(double dt) {
    rewind_acc_ += dt * kEmuFps;
    const int n = std::min((int)rewind_acc_, 4);
    rewind_acc_ -= (int)rewind_acc_;
    bool got = false;
    for (int i = 0; i < n && rewind_.pop(rewind_state_); i++) { got = true; stat_pops_++; }
    if (!got) return;
    if (!session_->load_state(rewind_state_)) { rewind_.clear(); return; }
    session_->run_frame(0, 0);
    rewind_tick_ = 0;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void App::frame(double dt) {
    time_ += dt;
    if (autotest_) { if (!autotest_realtime_) dt = 1.0 / kEmuFps; run_script(); }   // one game frame per loop
    loop_frame_++;
    input_.poll();
    UiInput ui = poll_ui((float)dt);
    menu_.footer = "\xE2\x86\x91\xE2\x86\x93 Select    \xE2\x86\x90\xE2\x86\x92 Change    Enter / (A) Confirm    Esc / (B) Back";

    // Global hotkeys
    if (input_.key_pressed(VK_F11)) { settings_.fullscreen = !is_fullscreen_; set_fullscreen(settings_.fullscreen); }
    if (input_.key_pressed(VK_F12)) screenshot();
    const bool in_play = mode_ == Mode::Playing && session_ && !menu_.active() && !jukebox_.active();
    bool rewind_held = hold_hotkey('R', PAD_LT), ff_held = hold_hotkey(VK_TAB, PAD_RT);
#ifdef __ANDROID__
    rewind_held = rewind_held || touch_.held(TouchControls::BtnRewind);
    ff_held = ff_held || touch_.held(TouchControls::BtnFF);
    touch_.in_game = in_play;
    touch_.show_ff = settings_.fast_forward;
    touch_.show_rewind = settings_.rewind;
    touch_.layout((float)video_.width(), (float)video_.height());
    menu_.touch_ui = jukebox_.touch_ui = touch_active_;
    if (touch_active_) menu_.footer = "Tap an item to choose    Drag to scroll    Back to return";
    if (picking_roms_) poll_pick();
#endif
    rewinding_ = in_play && settings_.rewind && rewind_held;
    fast_forward_ = in_play && !rewinding_ && settings_.fast_forward && ff_held;
    if (!rewinding_) rewind_acc_ = 1.0;   // the first rewind step comes at once

    // How many emulated frames this display frame: lock-step with a ~60 Hz vsync'd display,
    // otherwise accumulate real time.
    int frames;
    double refresh = refresh_rate();
#ifdef __ANDROID__
    // Phones often run at 120 Hz: there each game frame is shown for exactly two refreshes (refresh_rate() is
    // measured there, so a phone that drops to 60 Hz or 90 Hz is followed).
    const int per = std::max(1, (int)std::lround(refresh / kEmuFps));
#else
    const int per = 1;
#endif
    if (settings_.vsync && !fast_forward_ && std::fabs(refresh - per * kEmuFps) < per * 1.0) {
        frames = ++vsync_phase_ % per == 0 ? 1 : 0;
        frame_acc_ = 0;
    } else {
        // Fast forward runs at 4x real time, whatever the display's refresh rate.
        frame_acc_ += dt * kEmuFps * (fast_forward_ ? 4.0 : 1.0);
        frames = (int)frame_acc_;
        frame_acc_ -= frames;
        frames = std::min(frames, fast_forward_ ? 12 : 3);
    }

    // Audio rate control: keep ~50 ms queued.
    const double target = 48000 * 0.05;
    double queued = audio_.queued_frames();
    double adjust = recording_ ? 1.0 : 1.0 + std::clamp((target - queued) / target, -1.0, 1.0) * 0.006;
    audio_buf_.clear();

    if (jukebox_.active()) {
        jukebox_.update(ui);
        if (jukebox_.game_switch()) {
            int dir = jukebox_.game_switch();
            jukebox_.clear_switch();
            int cur = 0;
            for (int g = 0; g < kGameCount; g++) if (jukebox_.module() == games_[g].module.get()) cur = g;
            int next = cur;
            for (int k = 0; k < kGameCount; k++) {
                next = (next + dir + kGameCount) % kGameCount;
                if (games_[next].rom.found) break;
            }
            if (next != cur) open_jukebox(next, false);
        }
        if (jukebox_.wants_close()) {
            jukebox_.close();
        } else {
            for (int i = 0; i < frames; i++) { jukebox_.step(); jukebox_.mix_audio(audio_buf_, adjust); }
        }
    } else if (mode_ == Mode::Playing && session_) {
        if (menu_.active()) {
            menu_.update(ui);
            if (!menu_.active()) block_game_input_ = true;
        } else if (ui.menu) {
            open_pause_menu();
        } else {
            if (input_.key_pressed(VK_F5)) quick_save();
            if (input_.key_pressed(VK_F9)) quick_load();
            if (block_game_input_ && !any_game_button_held()) block_game_input_ = false;
            if (rewinding_) rewind_step(dt);
            else run_game_frames(frames, true);
        }
    } else {
        if (menu_.active()) menu_.update(ui);
        else update_launcher(ui, dt);
        // attract-mode previews keep running behind the launcher
        for (int i = 0; i < frames; i++) {
            for (int g = 0; g < kGameCount; g++) {
                if (!games_[g].preview) continue;
                games_[g].preview->run_frame(0, 0);
                if (g == launcher_sel_ && !menu_.active()) games_[g].preview->mix_audio(audio_buf_, adjust);
            }
        }
    }

    if (!audio_buf_.empty()) {
        if (queued < target * 0.3) {
            // refill after pauses so we don't run dry
            std::vector<int16_t> silence((size_t)(target * 0.5 - queued) * 2, 0);
            audio_.push(silence.data(), (int)silence.size() / 2);
        }
        audio_.push(audio_buf_.data(), (int)audio_buf_.size() / 2);
    }

    // ---- render ----
    video_.begin_frame(0xFF000000);
    const float W = (float)video_.width(), H = (float)video_.height();
    if (jukebox_.active()) {
        jukebox_.hint_fade_ = menu_.toast_cover();
        jukebox_.draw(video_, (float)time_, jukebox_.module()->accent_color());
    } else if (mode_ == Mode::Playing && session_) {
        draw_game_view(0, *session_, {0, 0, W, H}, 1.0f, 0.0f);
#ifdef __ANDROID__
        if (!menu_.active() && settings_.touch_mode != 2)
            touch_.draw(video_, settings_.touch_opacity / 100.0f, settings_.fast_forward, settings_.rewind);
#endif
    } else {
        draw_launcher();
    }
    menu_.accent = accent();
    if (!jukebox_.active()) menu_.draw(video_, (float)time_);   // the menu reappears when the player closes
    menu_.draw_toasts(video_, (float)dt, jukebox_.active() || mode_ != Mode::Playing || menu_.active());
    if (mode_ == Mode::Playing && session_ && !menu_.active() && !jukebox_.active() && (rewinding_ || fast_forward_))
        video_.draw_text(FontId::Small, H * 0.02f, H * 0.01f, rewinding_ ? "\xE2\x97\x80\xE2\x97\x80" : "\xE2\x96\xB6\xE2\x96\xB6",
                         0xFFFFFFC0, Align::Left);
    if (settings_.show_fps) {
        fps_frames_++;
        fps_timer_ += dt;
        if (fps_timer_ >= 0.5) { fps_display_ = fps_frames_ / fps_timer_; fps_frames_ = 0; fps_timer_ = 0; }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f fps", fps_display_);
        video_.draw_text(FontId::Small, W - H * 0.02f, H * 0.01f, buf, 0xFFFFFF80, Align::Right);
    }
    if (!pending_shot_.empty()) {
        std::vector<uint32_t> px;
        int w, h;
        if (video_.capture(px, w, h)) write_png(path_join(autotest_dir_, from_utf8(pending_shot_) + L".png"), px.data(), w, h);
        pending_shot_.clear();
    }
    if (recording_) record_frame();
#ifdef __ANDROID__
    work_end_ = SDL_GetPerformanceCounter();   // the frame's work, before the wait for the display (perf log)
#endif
    video_.end_frame();
}

bool App::load_script(const std::wstring& path) {
    std::string text;
    if (!read_text(path, text)) return false;
    std::istringstream f(text);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        char cmd[32] = {}, arg[128] = {};
        int fr = 0;
        if (sscanf(line.c_str(), "%d %31s %127s", &fr, cmd, arg) < 2) continue;
        std::string c = cmd, a = arg;
        auto vk_of = [](const std::string& n) -> int {
            static const struct { const char* n; int vk; } names[] = {
                {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"enter", VK_RETURN},
                {"esc", VK_ESCAPE}, {"space", VK_SPACE}, {"tab", VK_TAB}, {"back", VK_BACK}, {"f5", VK_F5},
                {"f9", VK_F9}, {"pgup", VK_PRIOR}, {"pgdn", VK_NEXT}};
            for (auto& e : names) if (n == e.n) return e.vk;
            if (n.size() == 1) return toupper((unsigned char)n[0]);
            return (int)strtol(n.c_str(), nullptr, 0);
        };
        if (c == "down" || c == "up") script_.push_back({fr, vk_of(a), c == "down" ? 1 : 0, ""});
        else if (c == "tap") { script_.push_back({fr, vk_of(a), 1, ""}); script_.push_back({fr + 3, vk_of(a), 0, ""}); }
        else if (c == "shot") script_.push_back({fr, 0, 2, a});
        else if (c == "quit") script_.push_back({fr, 0, 3, ""});
        else if (c == "realtime") script_.push_back({fr, 0, 4, a});   // "1": use real elapsed time (speed tests)
        else if (c == "mark") script_.push_back({fr, 0, 5, a});       // log time + frames run / rewound to marks.txt
        else if (c == "record") script_.push_back({fr, 0, 6, a});     // trailer footage: <a>.vid / <a>.pcm
        else if (c == "stoprec") script_.push_back({fr, 0, 7, ""});
        else if (c == "loadstate") script_.push_back({fr, 0, 8, a});  // <dir>\<a>.state into the running game
        else if (c == "pads") script_.push_back({fr, 0, 9, a});       // <dir>\<a>.pads: uint16 p1, p2 per frame
        else if (c == "set") script_.push_back({fr, 0, 10, a});       // key=value: widescreen, color, filter, hud_anchor
    }
    std::stable_sort(script_.begin(), script_.end(), [](const ScriptStep& x, const ScriptStep& y) { return x.frame < y.frame; });
    return true;
}

void App::run_script() {
    while (script_pos_ < script_.size() && script_[script_pos_].frame <= loop_frame_) {
        const ScriptStep& s = script_[script_pos_++];
        switch (s.action) {
        case 0: input_.on_key(s.vk, false); break;
        case 1: input_.on_key(s.vk, true); break;
        case 2: pending_shot_ = s.arg; break;
        case 4: autotest_realtime_ = s.arg != "0"; break;
        case 5: {
            std::ostringstream m;
            double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            uint32_t ram_hash = 2166136261u;   // FNV-1a of 68000 RAM, to check a replay against the bot's run
            if (session_) for (uint8_t b : session_->md().ram) ram_hash = (ram_hash ^ b) * 16777619u;
            m << s.arg << " " << std::fixed << t << " loop " << loop_frame_ << " runs " << stat_runs_ << " pops " << stat_pops_
              << " ram " << std::hex << ram_hash << std::dec << "\n";
            if (FILE* f = open_file(path_join(autotest_dir_, L"marks.txt"), "ab")) {
                fputs(m.str().c_str(), f);
                fclose(f);
            }
            break;
        }
        case 6: start_recording(s.arg); break;
        case 7: stop_recording(); break;
        case 8: {
            std::vector<uint8_t> d;
            read_file(path_join(autotest_dir_, from_utf8(s.arg) + L".state"), d);
            if (session_ && !d.empty() && session_->load_state(d)) { rewind_.clear(); frame_acc_ = 0; }
            break;
        }
        case 10: {
            // live video setting changes, as the Options menu makes them (trailer before/after shots)
            size_t eq = s.arg.find('=');
            if (eq == std::string::npos) break;
            std::string k = s.arg.substr(0, eq);
            int v = atoi(s.arg.c_str() + eq + 1);
            if (k == "widescreen") settings_.widescreen = (WideMode)std::clamp(v, 0, (int)WideMode::Count - 1);
            else if (k == "color") settings_.color = (ColorMode)std::clamp(v, 0, (int)ColorMode::Count - 1);
            else if (k == "filter") settings_.filter = std::clamp(v, 0, 4);
            else if (k == "hud_anchor") settings_.hud_anchor = v != 0;
            apply_settings();
            break;
        }
        case 9: {
            std::vector<uint8_t> d;
            read_file(path_join(autotest_dir_, from_utf8(s.arg) + L".pads"), d);
            pad_stream_.assign(d.size() / 4, 0);
            if (!pad_stream_.empty()) memcpy(pad_stream_.data(), d.data(), pad_stream_.size() * 4);
            pad_pos_ = 0;
            break;
        }
        default: running_ = false; break;
        }
    }
}

void App::run_game_frames(int count, bool with_audio) {
    uint16_t p1 = block_game_input_ ? 0 : pad_state(0);
    // Only 2-player games get a second controller.
    uint16_t p2 = block_game_input_ || session_->module()->max_players() < 2 ? 0 : pad_state(1);
    const bool replay = pad_pos_ < pad_stream_.size();
    const double target = 48000 * 0.05;
    double queued = audio_.queued_frames();
    double adjust = recording_ ? 1.0 : 1.0 + std::clamp((target - queued) / target, -1.0, 1.0) * 0.006;
    for (int i = 0; i < count; i++) {
        if (replay && pad_pos_ < pad_stream_.size()) {   // autotest "pads": recorded input, one pair per frame
            p1 = pad_stream_[pad_pos_] & 0xFFFF;
            p2 = (uint16_t)(pad_stream_[pad_pos_] >> 16);
            pad_pos_++;
        } else if (replay) {
            p1 = p2 = 0;
        }
        session_->run_frame(p1, p2);
        stat_runs_++;
        if (settings_.rewind && ++rewind_tick_ >= 2) {
            rewind_tick_ = 0;
            rewind_.push(session_->save_state());
        }
        // During fast forward one game frame's audio in four is kept, so the sound queue stays at real time.
        if (with_audio && (!fast_forward_ || (ff_audio_tick_++ & 3) == 0)) session_->mix_audio(audio_buf_, adjust);
    }
}

Rect App::game_rect(int fw, int fh, Rect area) {
    if (settings_.aspect == AspectMode::Stretch) return area;
    float par = settings_.aspect == AspectMode::Crt4x3 ? (4.0f / 3.0f) / (320.0f / 224.0f) : 1.0f;
    float dw = fw * par, dh = (float)fh;
    float scale = std::min(area.w / dw, area.h / dh);
    if (settings_.integer_scale && scale >= 1.0f) scale = std::floor(scale);
    float w = dw * scale, h = dh * scale;
    return {std::floor(area.x + (area.w - w) * 0.5f), std::floor(area.y + (area.h - h) * 0.5f), w, h};
}

void App::draw_game_view(int slot, Session& s, Rect area, float alpha, float dim) {
    int fw, fh;
    const uint32_t* px = s.frame(fw, fh);
    video_.upload_frame_slot(slot, px, fw, fh, fw);
    Rect r = game_rect(fw, fh, area);
    video_.draw_game(slot, r, (ScaleFilter)settings_.filter, alpha, dim);
}

// ---------------------------------------------------------------------------
// Game control
// ---------------------------------------------------------------------------
void App::find_game_roms() {
    rom_sig_ = rom_dirs_signature();
    std::vector<RomSpec> specs;
    std::vector<RomMatch> found;
    for (auto& g : games_) {
        specs.push_back({g.module->rom_crc32(), g.module->rom_size(), g.module->rom_names()});
        found.push_back(std::move(g.rom));
    }
    find_roms(specs, found);
    for (int i = 0; i < kGameCount; i++) {
        GameSlot& g = games_[i];
        g.rom = std::move(found[i]);
        if (!g.rom.found || g.preview) continue;
        g.preview = std::make_unique<Session>();
        g.preview->set_audio_rate(48000);
        g.preview->open(g.module.get(), g.rom.data);
        g.preview->apply_settings(preview_settings(settings_));
    }
}

// ROMs copied in while the launcher is open show up within a second (the folders are listed, and only
// searched again when something in them changed).
void App::poll_roms(double dt) {
    bool missing = false;
    for (auto& g : games_) missing = missing || !g.rom.found;
    if (!missing || (rom_poll_ -= dt) > 0) return;
    rom_poll_ = 1.0;
    if (rom_dirs_signature() == rom_sig_) return;
    bool was[kGameCount];
    for (int i = 0; i < kGameCount; i++) was[i] = games_[i].rom.found;
    find_game_roms();
    for (int i = 0; i < kGameCount; i++)
        if (!was[i] && games_[i].rom.found) menu_.toast(std::string(games_[i].module->title()) + " ROM found");
}

void App::launch(int g) {
    if (!games_[g].rom.found) {
        menu_.toast(games_[g].rom.other_file.empty() ? "ROM not found - see the game card for details"
                                                     : "Wrong ROM version - see the game card for details");
        return;
    }
    session_ = std::make_unique<Session>();
    rewind_.clear();
    session_->set_audio_rate(48000);
    session_->open(games_[g].module.get(), games_[g].rom.data);
    session_->apply_settings(settings_);
    session_->power_on();
    current_ = g;
    mode_ = Mode::Playing;
    menu_.close();
    block_game_input_ = true;
    settings_.last_game = games_[g].module->id();
    save_settings();
}

void App::back_to_launcher() {
    session_.reset();
    mode_ = Mode::Launcher;
    menu_.close();
    current_ = -1;
}

std::wstring App::save_path(int slot) {
    std::wstring dir = path_join(data_dir(), L"saves");
    make_dir(dir);
    return path_join(dir, from_utf8(games_[current_].module->id()) + L"_slot" + std::to_wstring(slot) + L".state");
}

void App::quick_save() {
    if (!session_) return;
    std::vector<uint8_t> st = session_->save_state();
    if (write_file(save_path(state_slot_), st.data(), st.size())) {
        menu_.toast("State saved to slot " + std::to_string(state_slot_));
    } else {
        menu_.toast("Could not write save state");
    }
}

void App::quick_load() {
    if (!session_) return;
    std::vector<uint8_t> st;
    if (!read_file(save_path(state_slot_), st)) { menu_.toast("Slot " + std::to_string(state_slot_) + " is empty"); return; }
    if (session_->load_state(st)) {
        rewind_.clear();
        menu_.toast("State loaded from slot " + std::to_string(state_slot_));
    } else {
        menu_.toast("Save state is incompatible");
    }
}

void App::screenshot() {
    Session* s = session_ ? session_.get() : (launcher_sel_ < kGameCount && games_[launcher_sel_].preview ? games_[launcher_sel_].preview.get() : nullptr);
    if (!s) return;
    int w, h;
    const uint32_t* px = s->frame(w, h);
    std::wstring dir = path_join(data_dir(), L"screenshots");
    make_dir(dir);
    time_t t = time(nullptr);
    tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char name[64];
    snprintf(name, sizeof(name), "%04d%02d%02d_%02d%02d%02d.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    menu_.toast(write_png(path_join(dir, from_utf8(name)), px, w, h) ? "Screenshot saved" : "Could not write screenshot");
}
