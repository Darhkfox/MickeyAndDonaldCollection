// Windows: window, message loop, fullscreen / monitors, and the autotest's trailer recording.
#include "app.h"
#include <objbase.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

namespace {
App* g_app = nullptr;
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
                if (from_utf8(games_[g].module->id()) == argv[i + 1]) launch(g);
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
        const MonitorInfo& m = mons[find_monitor(mons, from_utf8(settings_.monitor))];
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

// One displayed frame of trailer footage (and its audio).
void App::record_frame() {
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

void App::start_recording(const std::string& name) {
    stop_recording();
    rec_name_ = autotest_dir_ + L"\\" + from_utf8(name);
    rec_pcm_ = _wfopen((rec_name_ + L".pcm").c_str(), L"wb");
    recording_ = rec_pcm_ != nullptr;
    rec_frames_ = 0; rec_samples_ = 0;
}

double App::refresh_rate() { return display_refresh(hwnd_); }

