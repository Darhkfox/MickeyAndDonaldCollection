// Per-game enhancement modules: ROM identification, widescreen patches and HUD
// re-anchoring, cheats, level select and sound/music tables.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

class MegaDrive;

struct LevelEntry {
    std::string group;      // e.g. "The Enchanted Forest"
    std::string name;       // e.g. "Act 1"
    unsigned variants = 0;  // bit n = available in level_variants()[n]; 0 = all
};

struct SoundEntry {
    int id;                 // driver command id
    std::string name;
    bool music;
    int  seconds = 0;       // approximate loop length / duration for the music player (0 = unknown)
};

struct CheatEntry {
    std::string key;        // stable id stored in settings.ini
    std::string name;
    std::string desc;
};

struct GameConfig {
    int  ext = 0;                        // widescreen: extra pixels on each side (0, 20, 40)
    bool hud_anchor = true;              // move HUD elements to the screen edges in widescreen
    std::map<std::string, bool> cheats;  // enabled cheats
    bool cheat(const char* k) const { auto it = cheats.find(k); return it != cheats.end() && it->second; }
};

// Reported by the module after every frame.
struct FrameInfo {
    bool gameplay = false;   // a screen where widescreen shows real content
    int  mask_left = 0;      // pixels of the left extension to black out (0..ext)
    int  mask_right = 0;     // pixels of the right extension to black out (0..ext)
    int  mask_lines = 0;     // edge masks cover lines [0, mask_lines) only (0 = all lines), e.g. to spare a status bar
    bool pillar_below = false; // lines [mask_lines, end) are pillarboxed (HUD shown at its original width)
};

class GameModule {
public:
    virtual ~GameModule() = default;

    virtual const char* id() const = 0;          // "coi" / "qs"
    virtual const char* title() const = 0;       // "Castle of Illusion"
    virtual const char* subtitle() const = 0;    // "Starring Mickey Mouse"
    virtual const char* year() const = 0;
    virtual int         max_players() const { return 1; }   // simultaneous players supported
    virtual int         max_ext() const { return 40; }      // widest widescreen extension supported (px per side)
    virtual float       audio_gain() const { return 1.0f; } // output trim so the games play at similar loudness
    virtual uint32_t    rom_crc32() const = 0;
    virtual uint32_t    rom_size() const = 0;
    virtual const char* rom_hint() const = 0;    // expected file description for error messages
    // Header names that mark any version of the game (RomSpec::names), to tell a wrong version from none
    virtual const char* rom_names() const = 0;
    virtual uint32_t    accent_color() const = 0; // UI theme colour 0xAARRGGBB

    // Install ROM patches / VDP hooks for the configuration. Called after power-on and
    // whenever the configuration changes (between frames).
    virtual void configure(MegaDrive& md, const GameConfig& cfg) = 0;
    // Whether configure(cfg) can be applied now without changing ROM code the 68000 is running or will
    // return into (see patches_safe); the session waits for a frame where it can.
    virtual bool can_configure(const MegaDrive& md, const GameConfig& cfg) const { (void)md; (void)cfg; return true; }
    virtual void pre_frame(MegaDrive& md, const GameConfig& cfg) { (void)md; (void)cfg; }
    virtual void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) { (void)md; (void)cfg; (void)info; }

    virtual std::vector<CheatEntry> cheats() const { return {}; }

    virtual std::vector<LevelEntry> levels() const { return {}; }
    // Optional start variants for the level select (e.g. player modes).
    virtual std::vector<std::string> level_variants() const { return {}; }
    // Power-cycles `md` and runs it (headless, fast) until the chosen level is starting.
    virtual bool start_level(MegaDrive& md, const GameConfig& cfg, int index, int variant) {
        (void)md; (void)cfg; (void)index; (void)variant; return false;
    }

    virtual std::vector<SoundEntry> sounds() const { return {}; }
    // Power-cycles `md` and runs it until the sound driver accepts commands, with no music playing.
    virtual void jukebox_boot(MegaDrive& md) { (void)md; }
    // Called every frame while the jukebox runs (keeps the game quiet / in a stable state).
    virtual void jukebox_frame(MegaDrive& md) { (void)md; }
    virtual void play_sound(MegaDrive& md, int id) { (void)md; (void)id; }
    virtual void stop_sound(MegaDrive& md) { (void)md; }
};

GameModule* create_coi_module();
GameModule* create_qs_module();
GameModule* create_woi_module();
constexpr int kGameCount = 3;
