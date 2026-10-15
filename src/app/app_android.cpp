// Android: SDL window and event loop, touch / controller / key events, app lifecycle, and the ROM picker.
#include "app.h"
#include "../platform/android/sdl_keys.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>

namespace {
// The widest widescreen mode that fits the screen (square pixels), for the first start. The display's own size,
// held landscape: the window can start out in another orientation or without the system bars' room.
WideMode widest_fitting() {
    SDL_DisplayMode d;
    if (SDL_GetDesktopDisplayMode(0, &d) != 0 || d.w <= 0 || d.h <= 0) return WideMode::W16x9;
    const float aspect = (float)std::max(d.w, d.h) / std::min(d.w, d.h);
    WideMode best = WideMode::Off;
    for (int m = 0; m < (int)WideMode::Count; m++)
        if ((320.0f + 2 * wide_ext((WideMode)m)) / 224.0f <= aspect + 0.01f) best = (WideMode)m;
    return best;
}

// Calls a static method of MDActivity (the SDL activity) through JNI.
jint call_activity_int(const char* name) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return -2;
    jclass cls = env->GetObjectClass(act);
    jint r = -2;
    if (jmethodID m = env->GetStaticMethodID(cls, name, "()I")) r = env->CallStaticIntMethod(cls, m);
    env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return r;
}

void call_activity_void(const char* name) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return;
    jclass cls = env->GetObjectClass(act);
    if (jmethodID m = env->GetStaticMethodID(cls, name, "()V")) env->CallStaticVoidMethod(cls, m);
    env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
}
}  // namespace

int App::run() {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   // Back is a key (menu / back), not "close the app"
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    window_ = SDL_CreateWindow("Mickey & Donald Collection", 0, 0, 0, 0,
                               SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) return 1;
    set_frame_rate();
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);

    settings_path_ = path_join(data_dir(), L"settings.ini");
    if (!settings_.load(settings_path_)) settings_.widescreen = widest_fitting();
    settings_.vsync = true;
    settings_.fullscreen = true;
    make_dir(path_join(data_dir(), L"roms"));

    games_[0].module.reset(create_coi_module());
    games_[1].module.reset(create_qs_module());
    games_[2].module.reset(create_woi_module());

    if (!video_.init(window_, w, h)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mickey & Donald Collection", "OpenGL ES 3.0 could not be started.", window_);
        return 1;
    }
    audio_.init(48000);
    input_.init(window_);
    touch_.visible = settings_.touch_mode != 2;

    find_game_roms();
    apply_settings();
    for (int g = 0; g < kGameCount; g++)
        if (settings_.last_game == games_[g].module->id()) launcher_sel_ = launcher_last_card_ = g;

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    int perf_frames = 0;
    double perf_time = 0, perf_work = 0, perf_max = 0;
    auto use_touch = [this](bool on) {
        touch_active_ = on;
        touch_.visible = on ? settings_.touch_mode != 2 : settings_.touch_mode == 1;
    };
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const float W = (float)video_.width(), H = (float)video_.height();
            switch (e.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_GL_GetDrawableSize(window_, &w, &h);
                    video_.resize(w, h);
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (e.key.repeat) break;
                int vk = vk_from_scancode(e.key.keysym.scancode);
                if (vk) input_.on_key(vk, e.type == SDL_KEYDOWN);
                if (e.key.keysym.scancode != SDL_SCANCODE_AC_BACK && e.type == SDL_KEYDOWN) use_touch(false);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                input_.on_device_change();
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                use_touch(false);
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (std::abs(e.caxis.value) > 20000) use_touch(false);
                break;
            case SDL_FINGERDOWN:
                use_touch(true);
                touch_.on_down((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H);
                break;
            case SDL_FINGERMOTION:
                touch_.on_move((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H);
                break;
            case SDL_FINGERUP:
                touch_.on_up((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H);
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                // Android may end the app while it is in the background: keep the settings
                save_settings();
                touch_.release_all();
                input_.on_focus_lost();
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                last = SDL_GetPerformanceCounter();
                refresh_n_ = 0;
                set_frame_rate();   // the surface is new after a pause
                break;
            default: break;
            }
        }
        if (!running_) break;
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        refresh_dt_[refresh_n_++ % 48] = (float)dt;
        if (dt > 0.25) dt = 0.25;
        frame(dt);
        // every 5 s to the log (adb logcat | grep MickeyDonald): the display rate and the time per frame
        Uint64 done = work_end_ ? work_end_ : SDL_GetPerformanceCounter();
        perf_frames++;
        perf_work += (double)(done - now) / freq;
        perf_max = std::max(perf_max, (double)(done - now) / freq);
        if ((perf_time += dt) >= 5.0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "MickeyDonald: %.1f fps, work per frame %.2f ms avg / %.2f ms max",
                        perf_frames / perf_time, perf_work / perf_frames * 1000, perf_max * 1000);
            perf_frames = 0;
            perf_time = perf_work = perf_max = 0;
        }
    }
    save_settings();
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

void App::pick_roms() {
    picking_roms_ = true;
    call_activity_void("pickRoms");
}

// The picker runs over the app (which is paused meanwhile); when it closes, the copied files are searched.
void App::poll_pick() {
    jint r = call_activity_int("pollPickResult");
    if (r == -1) return;
    picking_roms_ = false;
    if (r <= 0) return;
    bool was[kGameCount];
    for (int i = 0; i < kGameCount; i++) was[i] = games_[i].rom.found;
    find_game_roms();
    int added = 0;
    for (int i = 0; i < kGameCount; i++)
        if (!was[i] && games_[i].rom.found) {
            menu_.toast(std::string(games_[i].module->title()) + " ROM found");
            added++;
        }
    if (!added) menu_.toast(r == 1 ? "That file isn't one of the ROMs - see the game cards" : "No new ROMs in those files - see the game cards");
}

// Android has one full-screen window at the display's own rate.
void App::set_fullscreen(bool) { is_fullscreen_ = true; }
void App::resize_window_to_scale() {}
void App::move_to_monitor(int) {}
// The rate the display really runs the app at (phones change it on their own: 120 Hz, 60 Hz when asked, ...):
// the median of the last 48 frame intervals, or the display mode's rate until there are enough.
double App::refresh_rate() {
    if (refresh_n_ >= 48) {
        float d[48];
        std::copy(refresh_dt_, refresh_dt_ + 48, d);
        std::nth_element(d, d + 24, d + 48);
        if (d[24] > 0.002f) return 1.0 / d[24];
    }
    SDL_DisplayMode m;
    if (SDL_GetCurrentDisplayMode(0, &m) == 0 && m.refresh_rate > 0) return m.refresh_rate;
    return 60.0;
}

// Tells Android the game runs at 60 fps (ANativeWindow_setFrameRate, Android 11+), so a phone with an adaptive
// display can run it at 60 Hz, or at a multiple of it, instead of whatever rate it would pick.
void App::set_frame_rate() {
    typedef int32_t (*SetFrameRate)(ANativeWindow*, float, int8_t);
    static SetFrameRate fn = (SetFrameRate)dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (fn && window_ && SDL_GetWindowWMInfo(window_, &info) && info.subsystem == SDL_SYSWM_ANDROID && info.info.android.window)
        fn(info.info.android.window, (float)kEmuFps, 1 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE */);
}

// Trailer recording is a Windows test feature.
void App::start_recording(const std::string&) {}
void App::record_frame() {}
void App::stop_recording() {}

extern "C" int SDL_main(int, char*[]) {
    App app;
    return app.run();
}
