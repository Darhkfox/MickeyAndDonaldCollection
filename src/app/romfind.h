// Locates and verifies the user's ROM files (the port never ships game data).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RomSpec {
    uint32_t crc = 0, size = 0;
    // Header names (letters and digits only, upper case) that mark any version of the game; '|' separates
    // them, a leading '=' means the whole name. Used to tell "another version" apart from "not there".
    const char* names = "";
};

struct RomMatch {
    bool found = false;
    std::wstring path;           // the file it came from (the archive for a zipped ROM)
    std::vector<uint8_t> data;   // raw (de-interleaved) ROM
    // Not found: a file holding another version of the game (or a modified copy), and what it is.
    std::wstring other_file;
    std::string other_desc;      // "the European version", "the REV 01 version", ...
};

uint32_t crc32_of(const uint8_t* data, size_t n);
std::wstring exe_dir();
// Where settings, save states and screenshots go: next to the executable (portable), or
// %LOCALAPPDATA%\Mickey and Donald Collection when that folder is read-only (e.g. Program Files).
std::wstring data_dir();
// Searches the executable directory, its "roms" subfolder and the working directory (and its "roms")
// for files whose CRC32 matches. Accepts raw .bin/.md/.gen dumps, interleaved .smd dumps, dumps with a
// 512-byte copier header, and all of these inside .zip archives. Games already found in `out` are
// skipped; `out` has one entry per spec.
void find_roms(const std::vector<RomSpec>& specs, std::vector<RomMatch>& out);
// Changes whenever a file is added to, removed from or rewritten in the folders find_roms() searches.
uint64_t rom_dirs_signature();
