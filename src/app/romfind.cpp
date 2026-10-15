#include "romfind.h"
#include "unzip.h"
#include <cctype>
#include <cstring>
#include <cwctype>

uint32_t crc32_of(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// .smd: 512-byte header, then 16 KB blocks with odd bytes first, even bytes second.
static std::vector<uint8_t> deinterleave_smd(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(in.size() - 512);
    for (size_t b = 0; b < out.size() / 16384; b++) {
        const uint8_t* src = &in[512 + b * 16384];
        uint8_t* dst = &out[b * 16384];
        for (int i = 0; i < 8192; i++) {
            dst[i * 2 + 1] = src[i];
            dst[i * 2] = src[8192 + i];
        }
    }
    return out;
}

namespace {
// Mega Drive ROM sizes, as dumped (a copier header adds 512 bytes).
bool plausible_size(uint64_t n) {
    return n >= 128 * 1024 && n <= (8u << 20) + 512 && (n % 16384 == 0 || n % 16384 == 512);
}

bool has_ext(const std::wstring& name, const wchar_t* ext) {
    size_t n = wcslen(ext);
    if (name.size() <= n) return false;
    for (size_t i = 0; i < n; i++)
        if (towlower(name[name.size() - n + i]) != towlower(ext[i])) return false;
    return true;
}

// The ROM images a dump can stand for: as it is, or with a 512-byte copier header dropped
// (interleaved .smd, or plain).
std::vector<std::vector<uint8_t>> raw_variants(std::vector<uint8_t>&& d) {
    std::vector<std::vector<uint8_t>> v;
    if (d.size() > 512 && d.size() % 16384 == 512) {
        v.push_back(deinterleave_smd(d));
        v.emplace_back(d.begin() + 512, d.end());
    } else {
        v.push_back(std::move(d));
    }
    return v;
}

std::string header_name(const std::vector<uint8_t>& r, size_t at) {
    std::string s;
    for (size_t i = at; i < at + 48; i++) {
        char c = (char)toupper(r[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) s += c;
    }
    return s;
}

// Does the header name the game (in any version)?
bool header_matches(const std::vector<uint8_t>& r, const char* names) {
    if (r.size() < 0x200) return false;
    bool sega = false;
    for (size_t i = 0x100; i + 4 <= 0x110; i++) sega = sega || memcmp(&r[i], "SEGA", 4) == 0;
    if (!sega) return false;
    const std::string dom = header_name(r, 0x120), ovs = header_name(r, 0x150);
    const std::string list = names;
    for (size_t a = 0; a < list.size();) {
        size_t b = list.find('|', a);
        if (b == std::string::npos) b = list.size();
        std::string key = list.substr(a, b - a);
        a = b + 1;
        if (key.empty()) continue;
        const bool whole = key[0] == '=';
        if (whole) key.erase(0, 1);
        for (const std::string* nm : {&dom, &ovs})
            if (whole ? *nm == key : nm->find(key) != std::string::npos) return true;
    }
    return false;
}

// What another dump of the game is, from its header's region and revision.
std::string describe_dump(const std::vector<uint8_t>& r) {
    bool jp = false, us = false, eu = false, letters = false;
    for (int i = 0; i < 3; i++) {
        char c = (char)r[0x1F0 + i];
        if (c == 'J') jp = letters = true;
        else if (c == 'U') us = letters = true;
        else if (c == 'E') eu = letters = true;
    }
    if (!letters) {   // later carts: one hex digit, bit 0 Japan, bit 2 Americas, bit 3 Europe
        char c = (char)r[0x1F0];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v >= 0) { jp = (v & 1) != 0; us = (v & 4) != 0; eu = (v & 8) != 0; }
    }
    if (!us && eu) return "the European version";
    if (!us && jp) return "the Japanese version";
    // Serial "GM 00004054-01": the last two digits are the revision.
    const char* sn = (const char*)&r[0x180];
    if (sn[11] == '-' && isdigit((unsigned char)sn[12]) && isdigit((unsigned char)sn[13]) && (sn[12] != '0' || sn[13] != '0'))
        return std::string("the REV ") + sn[12] + sn[13] + " version";
    return "a modified copy or a bad dump";
}

struct Scan {
    const std::vector<RomSpec>& specs;
    std::vector<RomMatch>& out;

    bool missing(size_t g) const { return !out[g].found; }
    bool done() const {
        for (size_t g = 0; g < out.size(); g++)
            if (missing(g)) return false;
        return true;
    }
    // A dump read from `file`: the ROM a game needs, or else maybe another version of one.
    void consider(std::vector<uint8_t>&& dump, const std::wstring& file) {
        auto variants = raw_variants(std::move(dump));
        for (auto& r : variants) {
            const uint32_t crc = crc32_of(r.data(), r.size());
            for (size_t g = 0; g < specs.size(); g++)
                if (missing(g) && r.size() == specs[g].size && crc == specs[g].crc) {
                    out[g].found = true;
                    out[g].path = file;
                    out[g].data = std::move(r);
                    out[g].other_file.clear();
                    out[g].other_desc.clear();
                    return;
                }
        }
        for (auto& r : variants)
            for (size_t g = 0; g < specs.size(); g++)
                if (missing(g) && out[g].other_file.empty() && header_matches(r, specs[g].names)) {
                    out[g].other_file = file;
                    out[g].other_desc = describe_dump(r);
                    return;
                }
    }
    void zip(const std::wstring& path) {
        std::vector<ZipEntry> entries;
        if (!zip_list(path, entries)) return;
        std::vector<bool> read(entries.size(), false);
        std::vector<uint8_t> data;
        // Entries the central directory already shows to be a wanted ROM (size and CRC32), or a headered one.
        for (size_t i = 0; i < entries.size() && !done(); i++)
            for (size_t g = 0; g < specs.size(); g++)
                if (missing(g) && !read[i] &&
                    ((entries[i].size == specs[g].size && entries[i].crc == specs[g].crc) || entries[i].size == specs[g].size + 512)) {
                    read[i] = true;
                    if (zip_read(path, entries[i], data)) consider(std::move(data), path);
                }
        // The rest, for other versions of a game that is still missing (a bounded amount of unpacking).
        uint64_t budget = 48u << 20;
        for (size_t i = 0; i < entries.size() && !done(); i++) {
            if (read[i] || !plausible_size(entries[i].size) || entries[i].size > budget) continue;
            budget -= entries[i].size;
            read[i] = true;
            if (zip_read(path, entries[i], data)) consider(std::move(data), path);
        }
    }
};
}  // namespace

void find_roms(const std::vector<RomSpec>& specs, std::vector<RomMatch>& out) {
    out.resize(specs.size());
    for (auto& m : out)
        if (!m.found) { m.other_file.clear(); m.other_desc.clear(); }
    Scan scan{specs, out};
    std::vector<DirEntry> entries;
    for (const auto& dir : rom_dirs()) {
        if (!list_dir(dir, entries)) continue;
        for (const DirEntry& e : entries) {
            if (scan.done()) break;
            if (e.is_dir) continue;
            const std::wstring path = path_join(dir, e.name);
            if (has_ext(e.name, L".zip")) {
                scan.zip(path);
                continue;
            }
            if (!plausible_size(e.size)) continue;
            std::vector<uint8_t> data;
            if (read_file(path, data)) scan.consider(std::move(data), path);
        }
    }
}

uint64_t rom_dirs_signature() {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 1099511628211ull;
    };
    std::vector<DirEntry> entries;
    for (const auto& dir : rom_dirs()) {
        if (!list_dir(dir, entries)) { mix("-", 1); continue; }
        for (const DirEntry& e : entries) {
            if (e.is_dir) continue;
            mix(e.name.data(), e.name.size() * sizeof(wchar_t));
            mix(&e.size, sizeof(e.size));
            mix(&e.mtime, sizeof(e.mtime));
        }
    }
    return h;
}
