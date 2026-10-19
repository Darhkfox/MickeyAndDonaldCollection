#include "soundtrack.h"
#include "romfind.h"
#include "sys.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>

namespace {
std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
bool has_ext(const std::wstring& name, const wchar_t* ext) {
    const std::wstring n = lower(name), e = ext;
    return n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0;
}
std::wstring parent_dir(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : path.substr(0, s);
}

// ---- cue sheets -----------------------------------------------------------------------------------------------
struct CueTrack {
    int number = 0;
    std::wstring file;
    bool raw = false;
    int64_t start = 0, loop = 0;
    bool can_loop = true;
};

std::vector<CueTrack> parse_cue(const std::wstring& path) {
    std::vector<CueTrack> out;
    std::string text;
    if (!read_text(path, text)) return out;
    const std::wstring dir = parent_dir(path);
    std::wstring file;
    bool raw = false;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        line = line.substr(b);
        auto starts = [&](const char* k) { return line.compare(0, strlen(k), k) == 0; };
        if (starts("FILE ")) {
            size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            std::string name, type;
            if (q2 != std::string::npos) { name = line.substr(q1 + 1, q2 - q1 - 1); type = line.substr(q2 + 1); }
            else { size_t s = line.find(' ', 5); name = line.substr(5, s == std::string::npos ? std::string::npos : s - 5);
                   type = s == std::string::npos ? "" : line.substr(s); }
            file = path_join(dir, from_utf8(name));
            raw = type.find("BINARY") != std::string::npos;
        } else if (starts("TRACK ")) {
            CueTrack t;
            t.number = atoi(line.c_str() + 6);
            t.file = file;
            t.raw = raw;
            if (line.find("AUDIO") != std::string::npos && t.number > 0) out.push_back(t);
            else out.push_back(CueTrack{t.number, L"", false, 0, 0, false});   // a data track keeps its number
        } else if (starts("INDEX 01") && !out.empty()) {
            int mm = 0, ss = 0, ff = 0;
            if (sscanf(line.c_str() + 8, " %d:%d:%d", &mm, &ss, &ff) == 3) out.back().start = ((int64_t)(mm * 60 + ss) * 75 + ff) * 588;
        } else if (starts("REM LOOP") && !out.empty()) {
            out.back().loop = (int64_t)atoi(line.c_str() + 8) * 588;      // CD sectors from the track start
        } else if (starts("REM NOLOOP") && !out.empty()) {
            out.back().can_loop = false;
        }
    }
    return out;
}

bool exists(const std::wstring& path) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// The ROM CRC a BPS patch was made for (its second-to-last... the first of the three trailing CRC32s).
bool bps_source_crc(const std::wstring& path, uint32_t& crc) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    uint8_t head[4] = {}, tail[12] = {};
    const uint64_t n = file_size(f);
    bool ok = n > 16 && fread(head, 1, 4, f) == 4 && memcmp(head, "BPS1", 4) == 0 && file_seek(f, n - 12) &&
              fread(tail, 1, 12, f) == 12;
    fclose(f);
    if (ok) crc = tail[0] | (tail[1] << 8) | (tail[2] << 16) | ((uint32_t)tail[3] << 24);
    return ok;
}

// A game id from names (IPS patches carry no CRC): "castle" -> coi, "quack" -> qs, "world" + "illusion" -> woi.
std::string game_from_name(const std::wstring& s) {
    const std::wstring n = lower(s);
    if (n.find(L"quack") != std::wstring::npos) return "qs";
    if (n.find(L"castle") != std::wstring::npos) return "coi";
    if (n.find(L"world") != std::wstring::npos && n.find(L"illusion") != std::wstring::npos) return "woi";
    return "";
}
}  // namespace

// ---- finding packs --------------------------------------------------------------------------------------------
std::vector<SoundtrackPack> find_soundtracks(const std::vector<std::pair<std::string, uint32_t>>& games) {
    std::vector<SoundtrackPack> out;
    std::vector<std::wstring> seen;
    auto check = [&](const std::wstring& dir) {
        for (const auto& s : seen) if (lower(s) == lower(dir)) return;
        seen.push_back(dir);
        std::vector<DirEntry> entries;
        if (!list_dir(dir, entries)) return;
        std::wstring cue, patch;
        for (const DirEntry& e : entries) {
            if (e.is_dir) continue;
            if (cue.empty() && has_ext(e.name, L".cue")) cue = path_join(dir, e.name);
            if (patch.empty() && (has_ext(e.name, L".bps") || has_ext(e.name, L".ips"))) patch = path_join(dir, e.name);
        }
        if (cue.empty() || patch.empty()) return;
        SoundtrackPack pk;
        pk.dir = dir; pk.cue = cue; pk.patch = patch;
        pk.name = to_utf8(file_name(dir));
        uint32_t crc = 0;
        if (has_ext(patch, L".bps")) {
            if (!bps_source_crc(patch, crc)) return;
            for (const auto& g : games) if (g.second == crc) pk.game_id = g.first;
        } else {
            pk.game_id = game_from_name(file_name(dir) + L" " + file_name(patch) + L" " + file_name(cue));
        }
        if (pk.game_id.empty()) return;
        for (const CueTrack& t : parse_cue(cue))
            if (!t.file.empty() && exists(t.file)) pk.tracks++;
        if (pk.tracks > 0) out.push_back(pk);
    };
    std::vector<DirEntry> entries, sub;
    std::vector<std::wstring> soundtrack_roots = {path_join(exe_dir(), L"soundtracks"), path_join(data_dir(), L"soundtracks")};
    for (const auto& root : soundtrack_roots) {            // soundtracks\<pack>, or soundtracks\<download>\<pack>
        if (!list_dir(root, entries)) continue;
        for (const DirEntry& e : entries) {
            if (!e.is_dir) continue;
            const std::wstring d = path_join(root, e.name);
            check(d);
            if (list_dir(d, sub))
                for (const DirEntry& s : sub) if (s.is_dir) check(path_join(d, s.name));
        }
    }
    for (const auto& root : rom_dirs()) {                   // a pack folder placed next to the ROMs
        if (!list_dir(root, entries)) continue;
        for (const DirEntry& e : entries)
            if (e.is_dir && lower(e.name) != L"soundtracks") check(path_join(root, e.name));
    }
    std::sort(out.begin(), out.end(), [](const SoundtrackPack& a, const SoundtrackPack& b) {
        return a.game_id != b.game_id ? a.game_id < b.game_id : a.name < b.name;
    });
    return out;
}

std::string soundtrack_label(const std::string& name) {
    std::string n = name;
    if (n.compare(0, 4, "MD+ ") == 0) n = n.substr(4);
    const size_t a = n.rfind('('), b = n.rfind(')');
    const size_t by = n.rfind(" by ");
    if (a != std::string::npos && b != std::string::npos && b > a + 1) n = n.substr(a + 1, b - a - 1);
    else if (by != std::string::npos) n = "by " + n.substr(by + 4);
    if (n.size() > 34) n = n.substr(0, 32) + "\xE2\x80\xA6";
    return n;
}

// ---- ROM patches ----------------------------------------------------------------------------------------------
bool apply_rom_patch(const std::vector<uint8_t>& rom, const std::wstring& patch_path, std::vector<uint8_t>& out,
                     std::string& error) {
    std::vector<uint8_t> p;
    if (!read_file(patch_path, p)) { error = "can't read the patch"; return false; }
    if (p.size() >= 4 && memcmp(p.data(), "BPS1", 4) == 0) {
        if (p.size() < 16) { error = "the patch is cut short"; return false; }
        size_t pos = 4;
        const size_t end = p.size() - 12;
        bool bad = false;
        auto num = [&]() -> uint64_t {
            uint64_t data = 0, shift = 1;
            while (pos < end) {
                const uint8_t x = p[pos++];
                data += (x & 0x7F) * shift;
                if (x & 0x80) return data;
                shift <<= 7;
                data += shift;
            }
            bad = true;
            return 0;
        };
        auto crc_at = [&](size_t o) { return p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24); };
        const uint64_t src_size = num(), dst_size = num(), meta = num();
        pos += (size_t)meta;
        if (bad || src_size != rom.size() || dst_size > (16u << 20)) { error = "the patch is for another ROM"; return false; }
        if (crc32_of(rom.data(), rom.size()) != crc_at(p.size() - 12)) { error = "the patch is for another ROM"; return false; }
        out.assign((size_t)dst_size, 0);
        size_t o = 0, src_rel = 0, dst_rel = 0;
        while (pos < end && !bad) {
            const uint64_t d = num();
            const size_t len = (size_t)(d >> 2) + 1;
            if (o + len > out.size()) { bad = true; break; }
            switch (d & 3) {
            case 0:                                                    // SourceRead
                if (o + len > rom.size()) { bad = true; break; }
                memcpy(&out[o], &rom[o], len); o += len; break;
            case 1:                                                    // TargetRead
                if (pos + len > end) { bad = true; break; }
                memcpy(&out[o], &p[pos], len); pos += len; o += len; break;
            default: {
                const uint64_t r = num();
                const int64_t off = (r & 1 ? -1 : 1) * (int64_t)(r >> 1);
                if ((d & 3) == 2) {                                    // SourceCopy
                    src_rel = (size_t)((int64_t)src_rel + off);
                    if (src_rel + len > rom.size()) { bad = true; break; }
                    memcpy(&out[o], &rom[src_rel], len); src_rel += len; o += len;
                } else {                                               // TargetCopy (may overlap)
                    dst_rel = (size_t)((int64_t)dst_rel + off);
                    if (dst_rel >= o) { bad = true; break; }
                    for (size_t i = 0; i < len; i++) out[o++] = out[dst_rel++];
                }
            }
            }
        }
        if (bad || o != out.size()) { error = "the patch is damaged"; return false; }
        if (crc32_of(out.data(), out.size()) != crc_at(p.size() - 8)) { error = "the patched ROM doesn't check out"; return false; }
        return true;
    }
    if (p.size() >= 8 && memcmp(p.data(), "PATCH", 5) == 0) {         // IPS
        out = rom;
        size_t pos = 5;
        while (pos + 3 <= p.size()) {
            if (memcmp(&p[pos], "EOF", 3) == 0) {
                if (pos + 6 <= p.size()) out.resize((p[pos + 3] << 16) | (p[pos + 4] << 8) | p[pos + 5]);   // truncation
                return true;
            }
            if (pos + 5 > p.size()) break;
            const size_t at = (p[pos] << 16) | (p[pos + 1] << 8) | p[pos + 2];
            size_t len = (p[pos + 3] << 8) | p[pos + 4];
            pos += 5;
            if (len == 0) {                                            // run of one byte
                if (pos + 3 > p.size()) break;
                len = (p[pos] << 8) | p[pos + 1];
                if (out.size() < at + len) out.resize(at + len, 0);
                memset(&out[at], p[pos + 2], len);
                pos += 3;
            } else {
                if (pos + len > p.size()) break;
                if (out.size() < at + len) out.resize(at + len, 0);
                memcpy(&out[at], &p[pos], len);
                pos += len;
            }
        }
        error = "the patch is damaged";
        return false;
    }
    error = "not a BPS or IPS patch";
    return false;
}

// ---- MD+ player -----------------------------------------------------------------------------------------------
struct MdPlus::Stream {
    FILE* f = nullptr;
    std::wstring path;
    uint64_t data_off = 0;
    int64_t frames = 0;                  // in the file
    int channels = 2, rate = 44100;
    std::vector<int16_t> buf;            // decoded frames from buf_first on
    int64_t buf_first = 0;
    ~Stream() { if (f) fclose(f); }

    bool open(const std::wstring& p, bool raw) {
        path = p;
        f = open_file(p, "rb");
        if (!f) return false;
        const uint64_t size = file_size(f);
        if (raw) { data_off = 0; frames = (int64_t)(size / 4); return true; }
        uint8_t h[12];
        if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
        bool fmt_ok = false;
        uint64_t at = 12;
        while (at + 8 <= size) {
            uint8_t c[8];
            if (!file_seek(f, at) || fread(c, 1, 8, f) != 8) return false;
            const uint32_t len = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
            if (memcmp(c, "fmt ", 4) == 0) {
                uint8_t fm[40] = {};
                if (fread(fm, 1, std::min<uint32_t>(len, 40), f) < 16) return false;
                const int tag = fm[0] | (fm[1] << 8), bits = fm[14] | (fm[15] << 8);
                channels = fm[2] | (fm[3] << 8);
                rate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | (fm[7] << 24);
                const bool pcm = tag == 1 || (tag == 0xFFFE && len >= 26 && (fm[24] | (fm[25] << 8)) == 1);   // extensible: subformat
                fmt_ok = pcm && bits == 16 && (channels == 1 || channels == 2) && rate >= 8000 && rate <= 192000;
            } else if (memcmp(c, "data", 4) == 0) {
                if (!fmt_ok) return false;
                data_off = at + 8;
                frames = (int64_t)(std::min<uint64_t>(len, size - data_off) / (2 * channels));
                return true;
            }
            at += 8 + len + (len & 1);
        }
        return false;
    }

    // frame `i` of the file (buffered reads of ~0.1 s)
    bool frame(int64_t i, float& l, float& r) {
        if (i < 0 || i >= frames) return false;
        const int64_t n = (int64_t)buf.size() / channels;
        if (i < buf_first || i >= buf_first + n) {
            const int64_t want = std::min<int64_t>(4096, frames - i);
            buf.resize((size_t)(want * channels));
            if (!file_seek(f, data_off + (uint64_t)i * 2 * channels)) { buf.clear(); return false; }
            const size_t got = fread(buf.data(), 2 * channels, (size_t)want, f);
            buf.resize(got * channels);
            buf_first = i;
            if (got == 0) return false;
        }
        const int16_t* s = &buf[(size_t)(i - buf_first) * channels];   // little-endian file, little-endian CPU
        l = s[0] / 32768.0f;
        r = channels == 2 ? s[1] / 32768.0f : l;
        return true;
    }
};

MdPlus::MdPlus() {}
MdPlus::~MdPlus() {}

bool MdPlus::load(const SoundtrackPack& pack, std::string& error) {
    tracks_.clear();
    std::vector<CueTrack> cue = parse_cue(pack.cue);
    if (cue.empty()) { error = "no tracks in the cue sheet"; return false; }
    for (size_t i = 0; i < cue.size(); i++) {
        const CueTrack& c = cue[i];
        if (c.number <= 0 || c.number > 99) continue;
        if ((int)tracks_.size() < c.number) tracks_.resize(c.number);
        Track& t = tracks_[c.number - 1];
        t.file = c.file;
        t.raw = c.raw;
        t.start = c.start;
        t.loop = c.loop;
        t.can_loop = c.can_loop;
        t.end = -1;
        if (i + 1 < cue.size() && cue[i + 1].file == c.file && cue[i + 1].start > c.start) t.end = cue[i + 1].start;
    }
    stop();
    return true;
}

void MdPlus::stop() {
    cur_ = 0;
    paused_ = false;
    unlocked_ = false;
    volume_ = 1.0f;
    fade_ = 1.0f;
    fade_step_ = 0;
}

bool MdPlus::playing() const { return cur_ > 0 && !paused_; }

void MdPlus::write(uint32_t addr, uint16_t value, int size) {
    if (size != 2) return;
    if (addr == 0x3F7FA) unlocked_ = value == 0xCD54;
    else if (addr == 0x3F7FE && unlocked_) command(value >> 8, value & 0xFF);
}

void MdPlus::command(int cmd, int arg) {
    switch (cmd) {
    case 0x11: play(arg, false); break;                  // play once
    case 0x12: play(arg, true); break;                   // play, looping from the cue's REM LOOP point
    case 0x13:                                           // pause; arg = fade-out time in 1/75 s
        if (arg == 0 || !cur_) { paused_ = true; fade_ = 1.0f; fade_step_ = 0; }
        else fade_step_ = 75.0f / arg;                   // (per second; mix() turns it into per output frame)
        break;
    case 0x14: paused_ = false; fade_ = 1.0f; fade_step_ = 0; break;   // resume
    case 0x15: volume_ = arg / 255.0f; break;            // volume
    default: break;
    }
}

bool MdPlus::play(int track, bool loop, int64_t at) {
    cur_ = 0;
    if (track <= 0 || track > (int)tracks_.size() || tracks_[track - 1].file.empty()) return false;
    const Track& t = tracks_[track - 1];
    if (!st_ || st_->path != t.file) {
        st_.reset(new Stream);
        if (!st_->open(t.file, t.raw)) { st_.reset(); return false; }
    }
    cur_ = track;
    loop_ = loop && t.can_loop;
    paused_ = false;
    fade_ = 1.0f;
    fade_step_ = 0;
    pos_ = std::max<int64_t>(0, at);
    phase_ = 0;
    win_[0][0] = win_[0][1] = 0;
    for (int k = 1; k < 4; k++)
        if (!next_frame(win_[k][0], win_[k][1])) win_[k][0] = win_[k][1] = 0;
    return true;
}

// The next source frame of the playing track (looping back to the loop point); false at its end.
bool MdPlus::next_frame(float& l, float& r) {
    const Track& t = tracks_[cur_ - 1];
    const int64_t end = t.end >= 0 ? std::min(t.end, st_->frames) : st_->frames;
    if (t.start + pos_ >= end) {
        if (!loop_) return false;
        pos_ = t.loop < end - t.start ? t.loop : 0;
    }
    if (!st_->frame(t.start + pos_, l, r)) return false;
    pos_++;
    return true;
}

void MdPlus::mix(int16_t* out, int frames, double out_rate, float gain) {
    if (!cur_ || paused_ || !st_ || frames <= 0) return;
    const double step = st_->rate / out_rate;
    const float fade_per_frame = fade_step_ > 0 ? (float)(fade_step_ / out_rate) : 0.0f;
    int ended = 0;
    for (int i = 0; i < frames; i++) {
        const float t = (float)phase_, t2 = t * t, t3 = t2 * t;
        const float g = gain * volume_ * fade_;
        for (int c = 0; c < 2; c++) {                     // cubic Hermite through win_[0..3], between [1] and [2]
            const float p0 = win_[0][c], p1 = win_[1][c], p2 = win_[2][c], p3 = win_[3][c];
            const float v = p1 + 0.5f * t * (p2 - p0) + t2 * (p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3) +
                            t3 * (1.5f * (p1 - p2) + 0.5f * (p3 - p0));
            const int s = out[i * 2 + c] + (int)lrintf(v * g * 32767.0f);
            out[i * 2 + c] = (int16_t)std::clamp(s, -32768, 32767);
        }
        if (fade_per_frame > 0) {
            fade_ -= fade_per_frame;
            if (fade_ <= 0) { paused_ = true; fade_ = 1.0f; fade_step_ = 0; return; }
        }
        phase_ += step;
        while (phase_ >= 1.0) {
            phase_ -= 1.0;
            for (int k = 0; k < 3; k++) { win_[k][0] = win_[k + 1][0]; win_[k][1] = win_[k + 1][1]; }
            if (!next_frame(win_[3][0], win_[3][1])) {
                win_[3][0] = win_[3][1] = 0;
                if (++ended >= 3) { cur_ = 0; return; }   // the last frames have played out
            }
        }
    }
}

std::string MdPlus::state() const {
    if (!cur_) return "";
    char buf[64];
    // (the window holds the three frames before pos_: resume from the first of them)
    snprintf(buf, sizeof(buf), "t%d,%lld,%d,%d", cur_, (long long)std::max<int64_t>(0, pos_ - 3),
             (loop_ ? 1 : 0) | (paused_ ? 2 : 0), (int)lrintf(volume_ * 255));
    return buf;
}

void MdPlus::set_state(const std::string& s) {
    stop();
    int track = 0, flags = 0, vol = 255;
    long long at = 0;
    if (s.size() < 2 || s[0] != 't' || sscanf(s.c_str() + 1, "%d,%lld,%d,%d", &track, &at, &flags, &vol) < 3) return;
    if (!play(track, (flags & 1) != 0, at)) return;
    paused_ = (flags & 2) != 0;
    volume_ = std::clamp(vol, 0, 255) / 255.0f;
}
