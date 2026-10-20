// One running game: Mega Drive core + game module + frame/audio conversion.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../core/md.h"
#include "../game/game.h"
#include "settings.h"
#include "audio_mixer.h"
#include "soundtrack.h"

class Session {
public:
    Session();
    ~Session();

    bool open(GameModule* module, const std::vector<uint8_t>& rom);
    // An MD+ soundtrack for the game (open() was given the ROM with the pack's patch applied).
    bool attach_soundtrack(const SoundtrackPack& pack, std::string& error);
    bool has_soundtrack() const { return mdplus_ != nullptr; }
    MdPlus* soundtrack() { return mdplus_.get(); }   // (the Music Player plays its tracks directly)
    GameModule* module() const { return module_; }
    MegaDrive& md() { return *md_; }

    // Applies video/audio/cheat settings. Safe to call at any time between frames.
    void apply_settings(const Settings& s);
    void power_on();
    void soft_reset();
    bool start_level(int index, int variant = 0);

    // Runs one emulated frame with the given pad states (MdButton bits).
    void run_frame(uint16_t pad1, uint16_t pad2);
    // Converts the last frame to BGRA using the current palette; applies widescreen masks.
    const uint32_t* frame(int& w, int& h);
    // Resamples the last frame's audio to `out` (48 kHz stereo int16), appending.
    void mix_audio(std::vector<int16_t>& out, double adjust);
    void set_audio_rate(double out_rate);

    // `extra`: the app's own note stored with the state (the achievement run, see App::ach_state_tag).
    std::vector<uint8_t> save_state(const std::string& extra = std::string());
    bool load_state(const std::vector<uint8_t>& data, std::string* extra = nullptr);

    const FrameInfo& info() const { return info_; }
    const GameConfig& config() const { return cfg_; }
    int  ext() const { return cfg_.ext; }
    bool muted = false;

private:
    GameModule* module_ = nullptr;
    std::unique_ptr<MegaDrive> md_;
    // cfg_: what the ROM patches, VDP width and module hooks run with; want_: what the settings ask for.
    // A change waits (pending_) for a frame end where the 68000 is clear of the code it changes.
    GameConfig cfg_, want_;
    bool pending_ = false;
    void switch_config();
    void try_switch();
    FrameInfo info_;
    uint32_t lut_[2048];
    std::vector<uint32_t> pixels_;
    AudioMixer mixer_;
    std::unique_ptr<MdPlus> mdplus_;
    float soundtrack_gain_ = 0.7f;
    double out_rate_ = 48000.0;
    float  mask_l_ = 0, mask_r_ = 0;     // animated masks (pixels)
    int    mask_lines_ = 0;
    int    dim_l_ = 0, dim_r_ = 0;           // dimmed scenery strips (FrameInfo::dim_left / dim_right)
    bool   pillar_below_ = false;
    bool   configured_ = false;
    Settings last_;
};
