#include "sys.h"
#include <windows.h>
#include <shellapi.h>

std::string to_utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring path_join(const std::wstring& dir, const std::wstring& name) { return dir + L"\\" + name; }

std::wstring file_name(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

FILE* open_file(const std::wstring& path, const char* mode) {
    FILE* f = nullptr;
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen_s(&f, path.c_str(), m.c_str()) == 0 ? f : nullptr;
}

bool read_file(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    uint64_t n = file_size(f);
    out.resize((size_t)n);
    bool ok = n == 0 || fread(out.data(), 1, (size_t)n, f) == n;
    fclose(f);
    return ok;
}

bool read_text(const std::wstring& path, std::string& out) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) return false;
    out.assign(d.begin(), d.end());
    return true;
}

bool write_file(const std::wstring& path, const void* data, size_t n) {
    FILE* f = open_file(path, "wb");
    if (!f) return false;
    bool ok = n == 0 || fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

bool file_seek(FILE* f, uint64_t pos) { return _fseeki64(f, (long long)pos, SEEK_SET) == 0; }

uint64_t file_size(FILE* f) {
    long long cur = _ftelli64(f);
    _fseeki64(f, 0, SEEK_END);
    long long n = _ftelli64(f);
    _fseeki64(f, cur, SEEK_SET);
    return n < 0 ? 0 : (uint64_t)n;
}

void make_dir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

bool list_dir(const std::wstring& dir, std::vector<DirEntry>& out) {
    out.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        DirEntry e;
        e.name = fd.cFileName;
        e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

std::wstring data_dir() {
    static std::wstring dir;
    if (!dir.empty()) return dir;
    dir = exe_dir();
    HANDLE h = CreateFileW((dir + L"\\.write_test").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return dir;
    }
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return dir;
    dir = std::wstring(buf, n) + L"\\Mickey and Donald Collection";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// The executable directory, its "roms" subfolder and the working directory (and its "roms").
std::vector<std::wstring> rom_dirs() {
    std::vector<std::wstring> dirs = {exe_dir(), exe_dir() + L"\\roms"};
    wchar_t cwd[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
        dirs.push_back(cwd);
        dirs.push_back(std::wstring(cwd) + L"\\roms");
    }
    std::vector<std::wstring> out;     // the working directory is usually the exe's
    for (const auto& d : dirs) {
        bool dup = false;
        for (const auto& o : out) dup = dup || _wcsicmp(o.c_str(), d.c_str()) == 0;
        if (!dup) out.push_back(d);
    }
    return out;
}

void open_url(const char* url) {
    ShellExecuteW(nullptr, L"open", from_utf8(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
