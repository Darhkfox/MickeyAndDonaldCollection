// Small OS layer for the app: files, folders, paths and links. Paths are std::wstring on every platform
// (Windows file APIs take them directly; elsewhere they are converted to UTF-8 when a file is opened).
// Implementations: sys_win.cpp (Windows), sys_android.cpp (Android).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

std::string to_utf8(const std::wstring& w);
std::wstring from_utf8(const std::string& s);

std::wstring path_join(const std::wstring& dir, const std::wstring& name);
std::wstring file_name(const std::wstring& path);      // the last component

FILE* open_file(const std::wstring& path, const char* mode);
bool read_file(const std::wstring& path, std::vector<uint8_t>& out);
bool read_text(const std::wstring& path, std::string& out);
bool write_file(const std::wstring& path, const void* data, size_t n);
bool file_seek(FILE* f, uint64_t pos);
uint64_t file_size(FILE* f);
void make_dir(const std::wstring& path);

struct DirEntry {
    std::wstring name;
    uint64_t size = 0;
    uint64_t mtime = 0;      // last write, in the platform's own units (only compared for changes)
    bool is_dir = false;
};
// The folder's entries (without "." and ".."); false if it can't be read.
bool list_dir(const std::wstring& dir, std::vector<DirEntry>& out);

std::wstring exe_dir();
// Where settings, save states and screenshots go: on Windows next to the executable (portable), or
// %LOCALAPPDATA%\Mickey and Donald Collection when that folder is read-only (e.g. Program Files); on
// Android the app's internal storage.
std::wstring data_dir();
// The folders searched for ROMs, in order.
std::vector<std::wstring> rom_dirs();

void open_url(const char* url);
