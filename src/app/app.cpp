#include "app.h"
#include "png.h"
#include <objbase.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iterator>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

namespace {
App* g_app = nullptr;
const double kEmuFps = 53693175.0 / (3420.0 * 262.0);   // 59.9227 Hz
const wchar_t* kWindowClass = L"MickeyDonaldPortWindow";
const wchar_t* kWindowTitle = L"Mickey & Donald Collection v1.0";   // keep in step with kAppVersion

LRESULT CALLBACK wnd_thunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_app) return g_app->wndproc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

double display_refresh(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            return dm.dmDisplayFrequency == 59 ? 59.94 : (double)dm.dmDisplayFrequency;
    }
    return 60.0;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
}  // namespace

// ---------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------
int App::run(HINSTANCE inst, int show) {
    g_app = this;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    timeBeginPeriod(1);

    settings_path_ = data_dir() + L"\\settings.ini";
    settings_.load(settings_path_);

    games_[0].module.reset(create_coi_module());
    games_[1].module.reset(create_qs_module());
    games_[2].module.reset(create_woi_module());

    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 2 < argc; i++)
            if (wcscmp(argv[i], L"--autotest") == 0) {
                autotest_dir_ = argv[i + 1];
                autotest_ = load_script(argv[i + 2]);
                CreateDirectoryW(autotest_dir_.c_str(), nullptr);
            }
        for (int i = 1; i + 1 < argc; i++)
            if (wcscmp(argv[i], L"--capture-size") == 0 && swscanf(argv[i + 1], L"%dx%d", &capture_w_, &capture_h_) != 2)
                capture_w_ = capture_h_ = 0;
        LocalFree(argv);
        if (autotest_) { show = SW_HIDE; settings_.fullscreen = false; settings_.vsync = true; }
        else capture_w_ = capture_h_ = 0;
    }
    if (!create_window(inst, show)) return 1;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (!video_.init(hwnd_, rc.right - rc.left, rc.bottom - rc.top)) {
        MessageBoxW(hwnd_, L"Direct3D 11 could not be initialised.", kWindowTitle, MB_ICONERROR);
        return 1;
    }
    audio_.init(48000);
    input_.init(hwnd_);

    find_game_roms();
    apply_settings();
    if (settings_.fullscreen) set_fullscreen(true);
    for (int g = 0; g < kGameCount; g++)
        if (settings_.last_game == games_[g].module->id()) launcher_sel_ = launcher_last_card_ = g;

    // Command line: --game coi|qs starts a game directly.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i++) {
        if (wcscmp(argv[i], L"--game") == 0) {
            for (int g = 0; g < kGameCount; g++)
                if (widen(games_[g].module->id()) == argv[i + 1]) launch(g);
        }
    }
    LocalFree(argv);

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running_ = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (IsIconic(hwnd_)) {
            audio_.set_paused(true);
            Sleep(20);
            continue;
        }
        audio_.set_paused(false);
        frame(dt);
        if (!settings_.vsync) {
            // Software pacing when not synchronised to the display (fast forward too: it runs more game frames
            // per displayed frame, not more displayed frames).
            LARGE_INTEGER t;
            double target = 1.0 / kEmuFps;
            for (;;) {
                QueryPerformanceCounter(&t);
                double el = (double)(t.QuadPart - last.QuadPart) / freq.QuadPart;
                if (el >= target) break;
                if (target - el > 0.002) Sleep(1);
            }
        }
    }
    if (!autotest_) save_settings();
    stop_recording();
    jukebox_.close();
    session_.reset();
    for (auto& g : games_) g.preview.reset();
    input_.shutdown();
    audio_.shutdown();
    video_.shutdown();
    timeEndPeriod(1);
    return 0;
}

bool App::create_window(HINSTANCE inst, int show) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_thunk;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWindowClass;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);
    int ext = wide_ext(settings_.widescreen);
    int cw = (320 + ext * 2) * settings_.window_scale, ch = 224 * settings_.window_scale;
    if (capture_w_ > 0) { cw = capture_w_; ch = capture_h_; }
    RECT r = {0, 0, cw, ch};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    // Open centred on the chosen display.
    std::vector<MonitorInfo> mons = enum_monitors();
    RECT pos = {CW_USEDEFAULT, CW_USEDEFAULT, 0, 0};
    if (!mons.empty()) {
        const MonitorInfo& m = mons[find_monitor(mons, widen(settings_.monitor))];
        pos = centered_rect(m.work, r.right - r.left, r.bottom - r.top);
        if (capture_w_ > 0) { pos.left = m.work.left; pos.top = m.work.top; }
    }
    hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, pos.left, pos.top,
                            r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    return true;
}

void App::set_fullscreen(bool fs) {
    if (fs == is_fullscreen_) return;
    if (fs) {
        GetWindowPlacement(hwnd_, &windowed_placement_);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        ShowCursor(FALSE);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(hwnd_, &windowed_placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ShowCursor(TRUE);
    }
    is_fullscreen_ = fs;
    settings_.fullscreen = fs;
}

RECT App::centered_rect(const RECT& work, int w, int h) {
    int ww = work.right - work.left, wh = work.bottom - work.top;
    w = std::min(w, ww);
    h = std::min(h, wh);
    RECT r;
    r.left = work.left + (ww - w) / 2;
    r.top = work.top + (wh - h) / 2;
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}

void App::move_to_monitor(int index) {
    std::vector<MonitorInfo> mons = enum_monitors();
    if (index < 0 || index >= (int)mons.size()) return;
    const MonitorInfo& m = mons[index];
    if (is_fullscreen_) {
        SetWindowPos(hwnd_, HWND_TOP, m.rect.left, m.rect.top, m.rect.right - m.rect.left, m.rect.bottom - m.rect.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        // Leaving fullscreen later should land on this display too.
        RECT wr = windowed_placement_.rcNormalPosition;
        windowed_placement_.rcNormalPosition = centered_rect(m.work, wr.right - wr.left, wr.bottom - wr.top);
        windowed_placement_.showCmd = SW_SHOWNORMAL;
    } else {
        if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        RECT c = centered_rect(m.work, wr.right - wr.left, wr.bottom - wr.top);
        SetWindowPos(hwnd_, nullptr, c.left, c.top, c.right - c.left, c.bottom - c.top, SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    remember_monitor();
}

void App::remember_monitor() {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) {
        settings_.monitor.clear();
        for (const wchar_t* p = mi.szDevice; *p; p++) settings_.monitor += (char)*p;   // plain ASCII names
    }
}

void App::resize_window_to_scale() {
    if (is_fullscreen_ || capture_w_ > 0) return;
    int ext = wide_ext(settings_.widescreen);
    int cw = (320 + ext * 2) * settings_.window_scale, ch = 224 * settings_.window_scale;
    RECT r = {0, 0, cw, ch};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

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
// Window procedure
// ---------------------------------------------------------------------------
LRESULT App::wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        running_ = false;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && video_.width() > 0) video_.resize(LOWORD(lp), HIWORD(lp));
        return 0;
    // Losing focus releases all keys (no stuck keys); the hidden autotest window injects its keys and
    // never has focus, so it keeps them.
    case WM_ACTIVATEAPP:
        active_window_ = wp != 0;
        if (!active_window_ && !autotest_) input_.on_focus_lost();
        return 0;
    case WM_KILLFOCUS:
        if (!autotest_) input_.on_focus_lost();
        return 0;
    case WM_DEVICECHANGE:
        input_.on_device_change();
        return TRUE;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && !(lp & (1 << 30))) {
            settings_.fullscreen = !is_fullscreen_;
            set_fullscreen(settings_.fullscreen);
            return 0;
        }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
        input_.on_key((int)wp, true);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        input_.on_key((int)wp, false);
        return 0;
    case WM_SYSCHAR:
    case WM_MENUCHAR:
        return msg == WM_MENUCHAR ? MAKELRESULT(0, MNC_CLOSE) : 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;   // no beep/menu on Alt
        break;
    case WM_EXITSIZEMOVE:
        remember_monitor();   // dragging the window to another display selects that display
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 480;
        mm->ptMinTrackSize.y = 300;
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
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
    rewinding_ = in_play && settings_.rewind && hold_hotkey('R', PAD_LT);
    fast_forward_ = in_play && !rewinding_ && settings_.fast_forward && hold_hotkey(VK_TAB, PAD_RT);
    if (!rewinding_) rewind_acc_ = 1.0;   // the first rewind step comes at once

    // How many emulated frames this display frame: lock-step with a ~60 Hz vsync'd display,
    // otherwise accumulate real time.
    int frames;
    double refresh = display_refresh(hwnd_);
    if (settings_.vsync && !fast_forward_ && std::fabs(refresh - kEmuFps) < 1.0) {
        frames = 1;
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
        if (video_.capture(px, w, h)) write_png(autotest_dir_ + L"\\" + widen(pending_shot_) + L".png", px.data(), w, h);
        pending_shot_.clear();
    }
    if (recording_) {
        std::vector<uint32_t> px;
        int w, h;
        if (rec_frames_ == 0 && video_.capture(px, w, h) && !start_ffmpeg(w, h))
            rec_vid_ = _wfopen((rec_name_ + L".vid").c_str(), L"wb");
        if ((rec_pipe_ || rec_vid_) && (!px.empty() || video_.capture(px, w, h)) && (rec_frames_ == 0 || (w == rec_w_ && h == rec_h_))) {
            rec_w_ = w; rec_h_ = h;
            if (rec_pipe_) {
                DWORD n = 0;
                WriteFile(rec_pipe_, px.data(), (DWORD)(px.size() * 4), &n, nullptr);
            } else {
                fwrite(px.data(), 4, px.size(), rec_vid_);
            }
            // Audio as produced; silence where nothing played (menus, pauses) so it stays in step with the frames.
            fwrite(audio_buf_.data(), 2, audio_buf_.size(), rec_pcm_);
            rec_frames_++;
            rec_samples_ += audio_buf_.size() / 2;
            const uint64_t due = (uint64_t)(rec_frames_ * 48000.0 / kEmuFps);
            if (rec_samples_ + 801 < due) {
                std::vector<int16_t> z((size_t)(due - rec_samples_) * 2, 0);
                fwrite(z.data(), 2, z.size(), rec_pcm_);
                rec_samples_ = due;
            }
        }
    }
    video_.end_frame();
}

bool App::load_script(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return false;
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
            std::ofstream m(autotest_dir_ + L"\\marks.txt", std::ios::app);
            double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            uint32_t ram_hash = 2166136261u;   // FNV-1a of 68000 RAM, to check a replay against the bot's run
            if (session_) for (uint8_t b : session_->md().ram) ram_hash = (ram_hash ^ b) * 16777619u;
            m << s.arg << " " << std::fixed << t << " loop " << loop_frame_ << " runs " << stat_runs_ << " pops " << stat_pops_
              << " ram " << std::hex << ram_hash << std::dec << "\n";
            break;
        }
        case 6: {
            stop_recording();
            rec_name_ = autotest_dir_ + L"\\" + widen(s.arg);
            rec_pcm_ = _wfopen((rec_name_ + L".pcm").c_str(), L"wb");
            recording_ = rec_pcm_ != nullptr;
            rec_frames_ = 0; rec_samples_ = 0;
            break;
        }
        case 7: stop_recording(); break;
        case 8: {
            std::ifstream f(autotest_dir_ + L"\\" + widen(s.arg) + L".state", std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
            std::ifstream f(autotest_dir_ + L"\\" + widen(s.arg) + L".pads", std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            pad_stream_.assign(d.size() / 4, 0);
            if (!pad_stream_.empty()) memcpy(pad_stream_.data(), d.data(), pad_stream_.size() * 4);
            pad_pos_ = 0;
            break;
        }
        default: running_ = false; break;
        }
    }
}

// Trailer recording: frames are piped to a hidden ffmpeg (lossless H.264 in <name>.mkv) if it is on the PATH.
bool App::start_ffmpeg(int w, int h) {
    wchar_t exe[MAX_PATH];
    if (!SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, exe, nullptr)) return false;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 22)) return false;
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
    wchar_t fps[32];
    swprintf(fps, 32, L"%.6f", kEmuFps);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" -loglevel error -y -f rawvideo -pix_fmt bgra -video_size " +
                       std::to_wstring(w) + L"x" + std::to_wstring(h) + L" -framerate " + fps +
                       L" -i - -c:v libx264 -preset ultrafast -qp 0 -pix_fmt yuv444p \"" + rec_name_ + L".mkv\"";
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rd;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(rd);
    if (!ok) { CloseHandle(wr); return false; }
    CloseHandle(pi.hThread);
    rec_pipe_ = wr;
    rec_proc_ = pi.hProcess;
    return true;
}

// Closes the trailer recording and writes <name>.txt: width height frames fps.
void App::stop_recording() {
    if (!recording_) return;
    if (rec_pipe_) CloseHandle(rec_pipe_);
    if (rec_proc_) { WaitForSingleObject(rec_proc_, 120000); CloseHandle(rec_proc_); }
    if (rec_vid_) fclose(rec_vid_);
    if (rec_pcm_) fclose(rec_pcm_);
    std::ofstream info(rec_name_ + L".txt");
    info << rec_w_ << " " << rec_h_ << " " << rec_frames_ << " " << kEmuFps << "\n";
    rec_pipe_ = rec_proc_ = nullptr;
    rec_vid_ = rec_pcm_ = nullptr;
    recording_ = false;
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
    std::wstring dir = data_dir() + L"\\saves";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + widen(games_[current_].module->id()) + L"_slot" + std::to_wstring(slot) + L".state";
}

void App::quick_save() {
    if (!session_) return;
    std::vector<uint8_t> st = session_->save_state();
    std::ofstream f(save_path(state_slot_), std::ios::binary | std::ios::trunc);
    if (f) {
        f.write((const char*)st.data(), (std::streamsize)st.size());
        menu_.toast("State saved to slot " + std::to_string(state_slot_));
    } else {
        menu_.toast("Could not write save state");
    }
}

void App::quick_load() {
    if (!session_) return;
    std::ifstream f(save_path(state_slot_), std::ios::binary);
    if (!f) { menu_.toast("Slot " + std::to_string(state_slot_) + " is empty"); return; }
    std::vector<uint8_t> st((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
    std::wstring dir = data_dir() + L"\\screenshots";
    CreateDirectoryW(dir.c_str(), nullptr);
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    wchar_t name[64];
    swprintf(name, 64, L"\\%04d%02d%02d_%02d%02d%02d.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    menu_.toast(write_png(dir + name, px, w, h) ? "Screenshot saved" : "Could not write screenshot");
}
