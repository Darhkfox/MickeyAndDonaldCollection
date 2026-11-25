// Linux (desktop and Steam Deck): SDL window and event loop, fullscreen / window size, and the hidden autotest.
// The drawing, sound and controllers are the Android build's SDL / OpenGL ES layer (src/platform/sdl).
//   DisneyClassics [--game coi|qs|woi] [--autotest <out dir> <script> [--capture-size WxH]]
#include "app.h"
#include "../platform/sdl/sdl_keys.h"
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_args;

// The widest widescreen mode that fits the display (square pixels), for the first start: 16:10 on a Steam Deck.
WideMode widest_fitting() {
    SDL_DisplayMode d;
    if (SDL_GetDesktopDisplayMode(0, &d) != 0 || d.w <= 0 || d.h <= 0) return WideMode::W16x9;
    const float aspect = (float)std::max(d.w, d.h) / std::min(d.w, d.h);
    WideMode best = WideMode::Off;
    for (int m = 0; m < (int)WideMode::Count; m++)
        if ((320.0f + 2 * wide_ext((WideMode)m)) / 224.0f <= aspect + 0.01f) best = (WideMode)m;
    return best;
}

const std::string kTitle = std::string("Mickey & Donald Collection v") + kAppVersion;
}  // namespace

int App::run() {
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");   // (a windowed game shouldn't turn compositing off)
#ifdef SDL_HINT_APP_NAME
    SDL_SetHint(SDL_HINT_APP_NAME, "Mickey & Donald Collection");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    settings_path_ = path_join(data_dir(), L"settings.ini");
    if (!settings_.load(settings_path_)) settings_.widescreen = widest_fitting();
    make_dir(path_join(data_dir(), L"roms"));

    games_[0].module.reset(create_coi_module());
    games_[1].module.reset(create_qs_module());
    games_[2].module.reset(create_woi_module());

    for (size_t i = 0; i + 2 < g_args.size(); i++)
        if (g_args[i] == "--autotest") {
            autotest_dir_ = from_utf8(g_args[i + 1]);
            autotest_ = load_script(from_utf8(g_args[i + 2]));
            make_dir(autotest_dir_);
        }
    for (size_t i = 0; i + 1 < g_args.size(); i++)
        if (g_args[i] == "--capture-size" && sscanf(g_args[i + 1].c_str(), "%dx%d", &capture_w_, &capture_h_) != 2)
            capture_w_ = capture_h_ = 0;
    if (autotest_) { settings_.fullscreen = false; settings_.vsync = true; }
    else capture_w_ = capture_h_ = 0;

    // The window: the game at the window scale (or the capture size); fullscreen goes over it at once.
    const int ext = wide_ext(settings_.widescreen);
    int ww = capture_w_ > 0 ? capture_w_ : (320 + 2 * ext) * settings_.window_scale;
    int wh = capture_h_ > 0 ? capture_h_ : 224 * settings_.window_scale;
    SDL_DisplayMode dm;
    if (capture_w_ <= 0 && SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0)
        while (settings_.window_scale > 1 && (ww > dm.w || wh > dm.h - 60)) {
            settings_.window_scale--;
            ww = (320 + 2 * ext) * settings_.window_scale;
            wh = 224 * settings_.window_scale;
        }
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (autotest_ ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    window_ = SDL_CreateWindow(kTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!window_) {
        fprintf(stderr, "No window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window_, 480, 300);
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    if (!video_.init(window_, w, h)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle.c_str(),
                                 "OpenGL ES 3.0 could not be started (graphics drivers with OpenGL ES 3 support are needed).", window_);
        return 1;
    }
    audio_.init(48000);
    input_.init(window_);

    find_game_roms();
    scan_soundtracks();
    apply_settings();
    ach_startup();
    if (settings_.fullscreen) set_fullscreen(true);
    for (int g = 0; g < kGameCount; g++)
        if (settings_.last_game == games_[g].module->id()) launcher_sel_ = launcher_last_card_ = g;
    for (size_t i = 0; i + 1 < g_args.size(); i++)
        if (g_args[i] == "--game")
            for (int g = 0; g < kGameCount; g++)
                if (g_args[i + 1] == games_[g].module->id()) launch(g);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                    SDL_GL_GetDrawableSize(window_, &w, &h);
                    video_.resize(w, h);
                    break;
                // Losing focus releases all keys (no stuck keys); the hidden autotest window never has focus.
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    active_window_ = false;
                    if (!autotest_) input_.on_focus_lost();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED: active_window_ = true; break;
                default: break;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (e.key.repeat) break;
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    settings_.fullscreen = !is_fullscreen_;   // Alt+Enter
                    set_fullscreen(settings_.fullscreen);
                    break;
                }
                const int vk = vk_from_scancode(e.key.keysym.scancode);
                if (vk) input_.on_key(vk, e.type == SDL_KEYDOWN);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                input_.on_device_change();
                break;
            // The mouse (see App::mouse_in_): clicks, right clicks and the wheel for poll_ui; a hand over the launcher's
            // links (the logo / Patreon page, the update badge). A touch screen's taps (the Steam Deck's) come as clicks.
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN: {
                int cw = 1, ch = 1;   // (window points -> drawable pixels: HiDPI)
                SDL_GetWindowSize(window_, &cw, &ch);
                const int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                const float x = mx * (float)w / std::max(1, cw), y = my * (float)h / std::max(1, ch);
                if (e.type == SDL_MOUSEMOTION) {
                    if (mode_ == Mode::Launcher && (e.motion.xrel || e.motion.yrel)) mouse_shown_ = true;
                    static SDL_Cursor* hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
                    static SDL_Cursor* arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
                    const bool link = mode_ == Mode::Launcher && !menu_.active() && !jukebox_.active() &&
                                      (over_launcher_link(x, y) || over_update_badge(x, y));
                    SDL_SetCursor(link ? hand : arrow);
                } else if (e.button.button == SDL_BUTTON_LEFT) {
                    mouse_in_.click = true;
                    mouse_in_.x = x;
                    mouse_in_.y = y;
                    if (mode_ == Mode::Launcher) mouse_shown_ = true;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    mouse_in_.right = true;
                }
                break;
            }
            case SDL_MOUSEWHEEL:
                mouse_in_.wheel -= e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -(float)e.wheel.y : (float)e.wheel.y;
                break;
            default: break;
            }
        }
        if (!running_) break;
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
            audio_.set_paused(true);
            SDL_Delay(20);
            continue;
        }
        audio_.set_paused(false);
        frame(dt);
        if (cursor_visible() != cursor_shown_) update_cursor();
        if (!settings_.vsync && !autotest_) {
            // Software pacing when not synchronised to the display (fast forward runs more game frames per picture)
            const double target = 1.0 / (kEmuFps * hfr_subs_);
            for (;;) {
                const double el = (double)(SDL_GetPerformanceCounter() - last) / freq;
                if (el >= target) break;
                if (target - el > 0.002) SDL_Delay(1);
            }
        }
    }
    if (!autotest_) save_settings();
    jukebox_.close();
    session_.reset();
    for (auto& g : games_) g.preview.reset();
    input_.shutdown();
    audio_.shutdown();
    video_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

void App::set_fullscreen(bool fs) {
    if (!window_ || fs == is_fullscreen_) return;
    SDL_SetWindowFullscreen(window_, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    is_fullscreen_ = fs;
    settings_.fullscreen = fs;
    update_cursor();   // (fullscreen hides the pointer until the mouse moves on the launcher)
}

// The pointer: shown or hidden as cursor_visible() says (each frame, and when the display mode changes).
void App::update_cursor() {
    cursor_shown_ = cursor_visible();
    SDL_ShowCursor(cursor_shown_ ? SDL_ENABLE : SDL_DISABLE);
}

void App::resize_window_to_scale() {
    if (!window_ || is_fullscreen_ || capture_w_ > 0) return;
    const int ext = wide_ext(settings_.widescreen);
    SDL_SetWindowSize(window_, (320 + ext * 2) * settings_.window_scale, 224 * settings_.window_scale);
}

void App::move_to_monitor(int) {}   // (Windows only; a window manager moves windows between displays)

double App::refresh_rate() {
    if (test_refresh_ > 0) return test_refresh_;
    SDL_DisplayMode m;
    const int d = window_ ? SDL_GetWindowDisplayIndex(window_) : 0;
    if (SDL_GetCurrentDisplayMode(d < 0 ? 0 : d, &m) == 0 && m.refresh_rate > 1) return m.refresh_rate == 59 ? 59.94 : m.refresh_rate;
    return 60.0;
}

void App::apply_slang() {}   // (slang presets: Windows only, librashader's Direct3D 11 runtime)
MenuPage App::slang_page() { return MenuPage(); }

// Trailer recording is a Windows test feature.
void App::start_recording(const std::string&) {}
void App::record_frame() {}
void App::stop_recording() {}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) g_args.push_back(argv[i]);
    static App app;
    return app.run();
}
