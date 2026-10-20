// CD-quality soundtracks for the games: MD+ packs (a ROM patch, a .cue sheet and the music as WAV files) that the
// player drops into a "soundtracks" folder. The patched game asks for music through the MD+ cartridge overlay
// (a write of $CD54 to $3F7FA, then a command word to $3F7FE); MdPlus plays the track from the pack and mixes
// it with the game's own sound (its effects; the patch silences the FM music).
#pragma once
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct SoundtrackPack {
    std::string name;          // the pack's folder name (what the settings store)
    std::string game_id;       // "coi", "qs", ...
    std::wstring dir, cue, patch;
    int tracks = 0;            // tracks in the .cue whose file is there
};

// The folders searched for packs: "soundtracks" next to the executable and in the data folder, and the ROM folders
// (a pack folder placed directly in one). A pack is a folder (or a folder inside one) with a .cue and a .bps / .ips
// patch; its game is the one whose ROM CRC the BPS patch names (`games`: id and CRC of the expected ROM).
std::vector<SoundtrackPack> find_soundtracks(const std::vector<std::pair<std::string, uint32_t>>& games);

// A short name for menus: "MD+ Castle Of Illusion (Grant Kirkhope 2013 Arranged)" -> "Grant Kirkhope 2013 Arranged",
// "... Patch and Soundpack by GMKalandar" -> "by GMKalandar".
std::string soundtrack_label(const std::string& pack_name);

// The pack's tracks whose audio is there: number and a name (the cue's TITLE, else one made from the file name:
// "28-Boss_JackInTheBox.wav" -> "Boss - Jack In The Box"; "Track n" for tracks sharing one file).
std::vector<std::pair<int, std::string>> soundtrack_track_names(const SoundtrackPack& pack);

// Applies a BPS or IPS patch to `rom`. BPS checks the source and result CRCs.
bool apply_rom_patch(const std::vector<uint8_t>& rom, const std::wstring& patch_path, std::vector<uint8_t>& out,
                     std::string& error);

class MdPlus {
public:
    MdPlus();
    ~MdPlus();
    bool load(const SoundtrackPack& pack, std::string& error);
    // A 68000 write to cartridge space (MegaDrive::on_cart_write).
    void write(uint32_t addr, uint16_t value, int size);
    // Adds `frames` stereo frames at `out_rate` to `out` (the game's audio of the same moment), at `gain`.
    void mix(int16_t* out, int frames, double out_rate, float gain);
    void stop();               // power-on / reset
    bool playing() const;
    // The Music Player: plays a track directly (looping from the cue's loop point if `loop`); the playing track's
    // position and a track's length, in seconds.
    bool play_track(int track, bool loop) { return play(track, loop); }
    double position() const;
    double track_seconds(int track) const;
    // Save states: what plays and where ("t<track>,<frame>,<flags>,<volume>"; empty = nothing).
    std::string state() const;
    void set_state(const std::string& s);

private:
    struct Track {
        std::wstring file;
        bool raw = false;              // BINARY: raw CD audio (16-bit stereo, 44.1 kHz, little-endian)
        int64_t start = 0;             // first frame within the file (INDEX 01)
        int64_t end = -1;              // one past the last frame (-1: the end of the file / the next track)
        int64_t loop = 0;              // REM LOOP n: frames from the track start (n CD sectors of 588 frames)
        bool can_loop = true;          // REM NOLOOP
    };
    struct Stream;                     // an open track: file, format, read buffer
    std::vector<Track> tracks_;        // by track number - 1
    std::unique_ptr<Stream> st_;
    int cur_ = 0;                      // track playing (1-based; 0 none)
    bool loop_ = false, paused_ = false, unlocked_ = false;
    int64_t pos_ = 0;                  // next frame to read, relative to the track start
    double phase_ = 0;                 // resampler position between win_[1] and win_[2]
    float win_[4][2] = {};             // the four source frames around the read position (cubic)
    float volume_ = 1.0f;              // $15xx
    float fade_ = 1.0f, fade_step_ = 0; // $13xx: fading out, then pausing
    void command(int cmd, int arg);
    bool play(int track, bool loop, int64_t at = 0);
    bool next_frame(float& l, float& r);
};
