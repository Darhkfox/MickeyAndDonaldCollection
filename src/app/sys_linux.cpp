// Linux side of sys.h: the app's folders, links and the update check (the files part: sys_posix.cpp).
// Run as an AppImage ($APPIMAGE set) the program's own folder is a read-only mount, so settings and saves go to
// $XDG_DATA_HOME/MickeyAndDonaldCollection (~/.local/share/...) and ROMs are also looked for beside the .AppImage;
// otherwise it is portable like the Windows build (everything next to the executable while that folder is writable).
#include "sys.h"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {
std::wstring parent(const std::wstring& path) {
    const size_t s = path.find_last_of(L'/');
    return s == std::wstring::npos ? std::wstring(L".") : s == 0 ? std::wstring(L"/") : path.substr(0, s);
}

// The folder holding the .AppImage when run as one, else empty
std::wstring appimage_dir() {
    const char* a = getenv("APPIMAGE");
    return a && *a ? parent(from_utf8(a)) : std::wstring();
}
}  // namespace

std::wstring exe_dir() {
    static std::wstring dir;
    if (dir.empty()) {
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; dir = parent(from_utf8(buf)); }
        else dir = L".";
    }
    return dir;
}

std::wstring data_dir() {
    static std::wstring dir;
    if (!dir.empty()) return dir;
    if (appimage_dir().empty() && access(to_utf8(exe_dir()).c_str(), W_OK) == 0) return dir = exe_dir();
    std::string base;
    if (const char* x = getenv("XDG_DATA_HOME"); x && *x) base = x;
    else if (const char* h = getenv("HOME"); h && *h) base = std::string(h) + "/.local/share";
    else base = "/tmp";
    dir = from_utf8(base + "/MickeyAndDonaldCollection");
    make_dir(from_utf8(base));
    make_dir(dir);
    return dir;
}

std::wstring export_dir() { return data_dir(); }

// Beside the .AppImage (and its roms folder), the data folder's roms folder, the program's folder and the working
// directory, without repeats.
std::vector<std::wstring> rom_dirs() {
    std::vector<std::wstring> dirs;
    const std::wstring ai = appimage_dir();
    if (!ai.empty()) { dirs.push_back(ai); dirs.push_back(path_join(ai, L"roms")); }
    dirs.push_back(path_join(data_dir(), L"roms"));
    dirs.push_back(exe_dir());
    dirs.push_back(path_join(exe_dir(), L"roms"));
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) {
        dirs.push_back(from_utf8(cwd));
        dirs.push_back(path_join(from_utf8(cwd), L"roms"));
    }
    std::vector<std::wstring> out;
    for (const auto& d : dirs) {
        bool dup = false;
        for (const auto& o : out) dup = dup || o == d;
        if (!dup) out.push_back(d);
    }
    return out;
}

void open_url(const char* url) { SDL_OpenURL(url); }   // (xdg-open: a web page, or a folder in the file manager)

// Through the curl command (on SteamOS and nearly every desktop Linux); false if it isn't there or the page fails.
bool http_get(const char* url, std::string& out) {
    out.clear();
    std::string quoted = "'";
    for (const char* p = url; *p; p++) {
        if (*p == '\'') quoted += "'\\''";
        else quoted += *p;
    }
    quoted += "'";
    const std::string cmd = "curl -fsSL --max-time 8 -H 'User-Agent: MickeyAndDonaldCollection' " + quoted + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int rc = pclose(p);
    if (rc != 0) out.clear();
    return !out.empty();
}
