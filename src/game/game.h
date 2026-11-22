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
    std::vector<std::string> excludes;   // cheats switched off when this one is switched on (they don't work together)
};

// One release of a game the module can run (QuackShot: REV 00 and REV 01 are both supported). A game's first variant is
// its primary one: the launcher's preview and the Music Player use it unless the player picked another.
struct RomVariant {
    uint32_t crc = 0, size = 0;
    std::string label;      // "REV 00" (empty when the game has a single release)
    std::string hint;       // expected file description for messages: "the QuackShot (World REV 00) ROM"
};

// Bytes a ROM patch writes, at an offset of the patched ROM (an offset past the original's end is added code or data).
struct PatchEdit {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
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
    // Scenery outside the play area (a boss arena the 4:3 camera never shows past): pixels of the extension whose
    // scenery is shown dimmed instead of black (sprites there are left as they are), on the same lines as the masks.
    // What the strips hold is the module's business (e.g. mirrored scenery where the map has run out).
    int  dim_left = 0, dim_right = 0;
};

struct AchievementDef {
    const char* id;          // stable id stored in achievements.ini ("coi_timber")
    const char* name;
    const char* desc;
    int target = 0;          // counted achievements: the count to reach (shown as progress), else 0
    bool set = false;        // the progress is a set of things found (bits; shown as how many), see AchievementCtx::marked
};

// What a module's achievement_frame() works with. The app owns it per session: `scratch` is reset when the
// run is interrupted (level select, a loaded save state); `run_bits` belong to the playthrough and travel with
// its save states.
struct AchievementCtx {
    bool run_counts = false;     // this game started (power-on) with achievements on and they stayed on: no cheats,
                                 // level select or rewind since (the app only runs the checks then)
    bool level_select = false;   // in: a level was just started with the port's level select. Its set-up ran inside
                                 // GameModule::start_level, so the first frame seen is already gameplay (the module
                                 // clears it once used)
    int  players = 1;            // 2 for a two-player game (set by the module when it can tell)
    uint32_t run_bits = 0;       // the module's facts about the playthrough (e.g. "a life was lost")
    int32_t scratch[32] = {};    // the module's frame-to-frame memory
    std::vector<uint8_t> prev;   // the previous frame's 68000 RAM (64 KB; empty on the first frame), kept by the app
    std::vector<int> unlocked;   // out: indices into achievements() earned this frame
    std::vector<std::pair<int, int>> progress;   // out: (index, count): the count reached now (the best is kept)
    std::vector<int> counted;    // out: indices whose saved count goes up by one (kept across sessions; earned at target)
    std::vector<std::pair<int, int>> marked;     // out: (index, bit) found, for a `set` achievement (earned at target)
    void reset_scratch() { for (int32_t& v : scratch) v = 0; prev.clear(); }
};

// Read access to a 68000 RAM image (big-endian bytes, $FF0000-$FFFFFF), the current frame's or the previous one's.
struct RamView {
    const uint8_t* r;
    uint8_t  b(uint32_t a) const { return r[a & 0xFFFF]; }
    uint16_t w(uint32_t a) const { a &= 0xFFFF; return (uint16_t)((r[a] << 8) | r[(a + 1) & 0xFFFF]); }
    int16_t  sw(uint32_t a) const { return (int16_t)w(a); }
    uint32_t l(uint32_t a) const { return ((uint32_t)w(a) << 16) | w(a + 2); }
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
    // The releases that can be played (rom_crc32() / rom_size() / rom_hint() describe the first). Most games have one.
    virtual std::vector<RomVariant> rom_variants() const { return {{rom_crc32(), rom_size(), "", rom_hint()}}; }
    // MD+ soundtracks: a pack's patch is made for one release of a game. When a game has several, a patch can sometimes be
    // carried over: `edits` are the bytes it writes into release `from`'s ROM (`from_size` long), and `target` is release
    // `to`'s ROM, which they are written into (moved to where they belong, and grown if the patch adds after the end).
    // `orig`, when the caller has release `from`'s ROM, lets the places be checked against it. false: they can't be
    // (the caller then plays the game without the soundtrack).
    virtual bool can_port_patch(int from, int to) const { (void)from; (void)to; return false; }
    virtual bool port_patch(const std::vector<PatchEdit>& edits, uint32_t from_size, const std::vector<uint8_t>* orig,
                            std::vector<uint8_t>& target) const {
        (void)edits; (void)from_size; (void)orig; (void)target;
        return false;
    }
    // What names a save state's game: id(), plus the release when its states can't be shared with another one (a state
    // holds RAM and CPU state, which only fits the code that made it). `md` runs the release in question.
    virtual std::string state_id(const MegaDrive& md) const { (void)md; return id(); }
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
    // Asks for music `id` the way the game itself does, running its own request code (on a machine readied by
    // jukebox_boot). On an MD+ patched ROM that code is hooked and answers with commands to the CD audio overlay:
    // how the Music Player finds the soundtrack's track for each tune. False if the module can't.
    virtual bool music_request(MegaDrive& md, int id) { (void)md; (void)id; return false; }

    // Achievements: the list (order fixed: indices are what achievement_frame() reports), and the check run after
    // every emulated frame of a game being played with achievements on. Reads RAM only.
    virtual std::vector<AchievementDef> achievements() const { return {}; }
    virtual void achievement_frame(const MegaDrive& md, AchievementCtx& ctx) const { (void)md; (void)ctx; }
};

GameModule* create_coi_module();
GameModule* create_qs_module();
GameModule* create_woi_module();
constexpr int kGameCount = 3;
