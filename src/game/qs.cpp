// QuackShot Starring Donald Duck (World, REV 00 and REV 01) - enhancement module.
// Addresses and techniques: docs/re_qs_game.md, docs/re_qs_wide.md (REV 00's addresses; see "releases" below for REV 01).
// REV 01 support and many of the widescreen fixes are simplePsyduck's, from the Nintendo Switch port of this collection
// (github.com/simplePsyduck/MickeyAndDonaldCollection-NX; docs/re_qs_wide.md, "simplePsyduck's fixes").
#include <cstdlib>
#include "game.h"
#include "common.h"
#include "../core/md.h"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <algorithm>
#include <set>

namespace {

// RAM
constexpr uint32_t MODE_W = 0xFFFF18;     // game mode word; low byte selects the handler, bit 15 = initialised
constexpr uint32_t MODE = 0xFFFF19;
constexpr uint32_t MAP_STATE = 0xFFA344;  // world map sub-state (1 = idle)
constexpr uint32_t LOCATION = 0xFF8F34, ENTRY = 0xFF8F74, DEBUG_STAGE = 0xFF8F40;
constexpr uint32_t PLUNGER = 0xFFEE48, PLUNGER_PAL = 0xFFF594, PAL_DIRTY = 0xFFEE02;
constexpr uint32_t POPCORN = 0xFF8F90, GUM = 0xFF8F92, AMMO_DIRTY = 0xFFEE5B;
constexpr uint32_t FREE_MOVE = 0xFFEE07;
constexpr uint32_t QUACK = 0xFFEE62, QUACK_TIME = 0xFF8FA8, TEMPER = 0xFF8F9A;
// The code wide_patches adds goes to free ROM (Rev::code); where in it things are:
constexpr uint32_t CLAMP_AT = 0x000, ON_SCREEN_AT = 0x200, WRAPPER_AT = 0x400, TRIGGER_AT = 0x4A0, HOOK_AT = 0x600;
constexpr uint32_t POST_AT = 0x700;    // the "spawns behind the player" option's routine after an object's X is set
constexpr uint32_t WAKE_AT = 0x800;    // ... and the golden Scrooge statues' wake-up distance, from where they will be
constexpr uint32_t CHARM_AT = 0x840, CWALL_AT = 0x870;   // ... the snake charmers' step direction / wall test while catching up
constexpr uint32_t DEDUP_AT = 0x900;   // the penguins' twin-entry check (see the entry hook)
// From this width on (16:9 and wider) the right spawn trigger also looks one 128-px cell further ("far" pass, see
// wide_patches): otherwise objects filed close to their cell's start (wide platforms, the South Pole penguins) are made
// at the old 4:3 edge, which a 16:9 view already shows.
constexpr int kFarFrom = 41;
// Two flag words in the spawn hook, each a "moveq #0/1,d6" (see wide_patches): 1 = the 4:3 column's pass creates all
// its entries (the first crossing after a turn), 0 = only the deferred ones. Right trigger / left trigger.
constexpr uint32_t SPAWN_ALL_AT = HOOK_AT + 2, LEFT_FLAG_AT = HOOK_AT + 6;
constexpr uint16_t LEFT_OFF = 0x7C00, LEFT_ON = 0x7C01;
constexpr uint32_t SFX_REQ = 0xFFEE52;    // sound effect waiting to go to the Z80 driver ($7FED8 sends it)
constexpr uint8_t SFX_PLANE = 0xB4;       // "airplane arrives", asked for by the plane object ($8024)
constexpr uint8_t M_TITLE = 0x01, M_MAP = 0x02, M_MAP_TO_LEVEL = 0x03, M_GAMEPLAY = 0x05, M_PAUSE = 0x08,
                  M_ENDING = 0x09, M_MENU = 0x0B, M_INTRO = 0x0C;
constexpr uint16_t PLUNGER_COLORS[3] = {0x02CE, 0x022E, 0x02A2};   // yellow / red / green

// ---- releases -------------------------------------------------------------------------------------------------
// REV 00 (512 KB) and REV 01 (1.25 MB) are the same program assembled twice: REV 01's code sits a little further up (a
// few bytes per edit, $200 by the end of the code), its data and sound banks $C0000 higher, and RAM is laid out the
// same - so everything in RAM, and every patch, works on both. All the ROM addresses in this file are REV 00's;
// Rev::at() gives the same place in the release being run (tools/re/qs_rev01.py derives and checks REV 01's table).
// Where a patched instruction holds a pc-relative displacement the shift changes, REV 01's value is in the Rev.
struct Site { uint32_t rev00, here; };

struct Rev {
    const char* name;
    const Site* sites;          // REV 00 address -> this release's, ascending (null: the same addresses)
    size_t      count;
    uint32_t    code;           // free ROM for the code wide_patches adds (REV 00 $7B000)
    uint16_t    call_5000, call_3f96;            // $35A0: displacements of "jsr $5000" / "jsr $3F96" (area load)
    uint16_t    stream_call_old, stream_call_new; // $3FCE: "jsr" displacement, as it is / retargeted into the new entry
    uint16_t    despawn_far, despawn_left;       // $9A9A: displacements of the object despawn's two branches
    uint32_t    sfx_bank, sfx_len;               // the sound effect bank (loaded at Z80 $0DD0): ROM address, length
    uint32_t    ending_bank;                     // the ending theme's bank (Z80 $0F00, $6D8 bytes)
    uint32_t    sound_request;                   // the game's sound request routine
    uint32_t at(uint32_t a) const {
        if (!sites) return a;
        const Site* end = sites + count;
        const Site* s = std::lower_bound(sites, end, a, [](const Site& x, uint32_t v) { return x.rev00 < v; });
        return s != end && s->rev00 == a ? s->here : 0;   // (0: not a site of this release - a patch there finds no match)
    }
    uint32_t spawn_all_flag() const { return code + SPAWN_ALL_AT; }
    uint32_t trigger() const { return code + TRIGGER_AT; }
    uint32_t left_flag() const { return code + LEFT_FLAG_AT; }
};

// REV 01's address for each REV 00 address this file passes through Rev::at() (tools/re/qs_rev01.py makes and checks it)
// BEGIN kRev01Sites (generated by tools/re/qs_rev01.py)
const Site kRev01Sites[] = {
    {0x0025E0, 0x00262C}, {0x002664, 0x0026B0}, {0x00266A, 0x0026B6}, {0x0026C8, 0x002714},
    {0x0026FC, 0x002748}, {0x0035A0, 0x0035EC}, {0x003F96, 0x003FE6}, {0x003FCE, 0x00401E},
    {0x0046CC, 0x004720}, {0x00481E, 0x004872}, {0x005000, 0x005086}, {0x005108, 0x005192},
    {0x005130, 0x0051BA}, {0x0051BA, 0x005244}, {0x005976, 0x005A44}, {0x00597E, 0x005A4C},
    {0x005E16, 0x005EE4}, {0x005FDE, 0x0060AC}, {0x00742C, 0x0074B6}, {0x009A9A, 0x009B64},
    {0x009BCE, 0x009C98}, {0x009BD4, 0x009C9E}, {0x009D66, 0x009E30}, {0x00B0C8, 0x00B1FC},
    {0x00B35E, 0x00B492}, {0x00B37E, 0x00B4B2}, {0x00B480, 0x00B5B4}, {0x00B4A6, 0x00B5DA},
    {0x00B5DE, 0x00B712}, {0x00B5F2, 0x00B726}, {0x00B866, 0x00B99A}, {0x00B8CE, 0x00BA02},
    {0x00CD52, 0x00CE86}, {0x00CDB0, 0x00CEE4}, {0x00CE92, 0x00CFC6}, {0x00CF02, 0x00D036},
    {0x00D308, 0x00D43C}, {0x00D340, 0x00D474}, {0x00D444, 0x00D578}, {0x00D4F6, 0x00D62A},
    {0x00D772, 0x00D8A6}, {0x00D7DA, 0x00D90E}, {0x00D7F6, 0x00D92A}, {0x00DB60, 0x00DC94},
    {0x00DCD0, 0x00DE04}, {0x00E6EC, 0x00E826}, {0x00E834, 0x00E96E}, {0x00E86E, 0x00E9A8},
    {0x00ECC4, 0x00EDFE}, {0x00F414, 0x00F57A}, {0x00FB7A, 0x00FCE0}, {0x01013A, 0x0102A0},
    {0x010148, 0x0102AE}, {0x01015A, 0x0102C0}, {0x01018A, 0x0102F0}, {0x010832, 0x010998},
    {0x010F12, 0x01108E}, {0x010FE4, 0x011160}, {0x010FF4, 0x011170}, {0x011032, 0x0111AE},
    {0x011042, 0x0111BE}, {0x01107E, 0x0111FA}, {0x01108E, 0x01120A}, {0x0110FA, 0x011276},
    {0x011918, 0x011A94}, {0x011FEC, 0x012168}, {0x011FF4, 0x012170}, {0x012164, 0x0122E0},
    {0x012258, 0x0123D4}, {0x013A6E, 0x013BEA}, {0x013AD6, 0x013C52}, {0x013F74, 0x0140F0},
    {0x0158CA, 0x015A46}, {0x019BEA, 0x019D74}, {0x019BFC, 0x019D86}, {0x019C14, 0x019D9E},
    {0x019C20, 0x019DAA}, {0x019C52, 0x019DDC}, {0x019C92, 0x019E1C}, {0x019CC4, 0x019E4E},
    {0x019D74, 0x019EFE}, {0x019D7C, 0x019F06},
};
// END kRev01Sites

const Rev kRev00 = {"REV 00", nullptr, 0, 0x07B000, 0x1A5E, 0x09F0, 0xE74C, 0xE73C, 0x1C22, 0x1C1C,
                    0x0792A6, 0xE1E, 0x07A0C4, 0x07FED8};
const Rev kRev01 = {"REV 01", kRev01Sites, sizeof(kRev01Sites) / sizeof(kRev01Sites[0]), 0x13F000, 0x1A98, 0x09F4, 0xE748, 0xE738,
                    0x1C8C, 0x1C86, 0x1392A6, 0xE24, 0x13A0CA, 0x13FED8};

// Which release `md` runs: the revision digits of the header serial ("GM 00004054-00" / "-01"). (Not the ROM size: an MD+
// patch may append to the ROM; and the header is the one place no patch of ours touches.)
const Rev& rev_of(const MegaDrive& md) {
    return md.rom.size() > 0x18D && md.rom[0x18C] == '0' && md.rom[0x18D] == '1' ? kRev01 : kRev00;
}

// Story-flag presets (cumulative, story order) - docs section 3.2.
enum Flag : uint16_t {
    HERO_KEY = 0xEEE0, VIKING_KEY = 0xEEE1, SPHINX_TEAR = 0xEEE2, SCEPTER_RA = 0xEEE3, NOTE_RED_PLUNGER = 0xEEE4,
    VIKING_DIARY = 0xEEE5, BUBBLEGUM = 0xEEE7, MEX_DOOR_OPEN = 0xEEEC, GREEN_PLUNGER = 0xEEED, TIGER_BEATEN = 0xEEEE,
    MET_SENORITA = 0xEEEF, TRANS_CASTLE_SEEN = 0xEEF0, TRANS_WALL_BLOWN = 0xEEF1, PALACE_SEEN = 0xEEF2,
    TREASURE_MAP = 0xEEF3, EGYPT_DOOR_OPEN = 0xEEF5, MET_GYRO = 0xEEF7, DIARY_RECOVERED = 0xEEF8,
    GO_HIDEOUT = 0xEEF9, DIARY_READ = 0xEEFA, MAP_DIPPED = 0xEEFB, VIKING_DOOR_OPEN = 0xEEFC,
};
inline uint16_t cleared(int loc) { return (uint16_t)(0xEEBF + loc); }

struct Preset { std::set<uint16_t> flags; int plunger; int gum; };
enum PresetId { P_NEW, P_MEX1, P_DUCK2, P_MEX2, P_DUCK3, P_TRANS, P_INDIA, P_EGYPT, P_SOUTH1, P_VIKING, P_SOUTH2, P_HIDE, P_ISLAND };

std::vector<Preset> build_presets() {
    std::vector<Preset> v;
    auto add = [&](std::initializer_list<uint16_t> f, int plunger, int gum, std::initializer_list<uint16_t> remove = {}) {
        Preset p;
        if (!v.empty()) p.flags = v.back().flags;
        for (uint16_t x : f) p.flags.insert(x);
        for (uint16_t x : remove) p.flags.erase(x);
        p.plunger = plunger;
        p.gum = gum;
        v.push_back(p);
    };
    add({}, 0, 0);
    add({cleared(1)}, 0, 0);
    add({cleared(2), MET_SENORITA}, 0, 0);
    add({HERO_KEY}, 0, 0);
    add({MEX_DOOR_OPEN, NOTE_RED_PLUNGER}, 1, 0);
    add({BUBBLEGUM, MET_GYRO}, 1, 50);
    add({cleared(3), TRANS_CASTLE_SEEN, TRANS_WALL_BLOWN, TREASURE_MAP}, 1, 50);
    add({cleared(6), PALACE_SEEN, TIGER_BEATEN, SPHINX_TEAR}, 1, 50);
    add({cleared(7), EGYPT_DOOR_OPEN, SCEPTER_RA}, 1, 50);
    add({cleared(5), VIKING_KEY}, 1, 50);
    add({cleared(4), VIKING_DOOR_OPEN, GREEN_PLUNGER}, 2, 50);
    add({GO_HIDEOUT}, 2, 50);
    add({DIARY_RECOVERED, VIKING_DIARY, DIARY_READ, MAP_DIPPED}, 2, 50, {GO_HIDEOUT});
    return v;
}

struct Level { int loc, entry, preset; const char* group; const char* name; };
// The game's own (unused) debug stage list, ROM $9DA6/$9DD6/$A06C.
const Level kLevels[] = {
    {1, 0, P_NEW, "Duckburg", "Streets"},
    {1, 1, P_NEW, "Duckburg", "Rooftops"},
    {1, 3, P_DUCK3, "Duckburg", "Gyro's Workshop"},
    {2, 0, P_MEX1, "Mexico", "Town"},
    {2, 1, P_MEX1, "Mexico", "Countryside"},
    {2, 2, P_MEX2, "Mexico", "Ruins Entrance"},
    {2, 3, P_MEX2, "Mexico", "Aztec Ruins"},
    {3, 0, P_TRANS, "Transylvania", "Area 1"},
    {3, 1, P_TRANS, "Transylvania", "Area 2"},
    {3, 2, P_TRANS, "Transylvania", "Area 3"},
    {3, 3, P_TRANS, "Transylvania", "Castle 1"},
    {3, 4, P_TRANS, "Transylvania", "Castle 2"},
    {3, 5, P_TRANS, "Transylvania", "Castle 3"},
    {3, 6, P_TRANS, "Transylvania", "Boss - Dracula Duck"},
    {4, 0, P_VIKING, "Viking Ship", "Deck 1"},
    {4, 1, P_VIKING, "Viking Ship", "Deck 2"},
    {4, 2, P_VIKING, "Viking Ship", "Deck 3"},
    {4, 3, P_VIKING, "Viking Ship", "Below Deck"},
    {4, 4, P_VIKING, "Viking Ship", "Boss - The Ghosts"},
    {5, 0, P_SOUTH2, "South Pole", "Area 1"},
    {5, 1, P_SOUTH2, "South Pole", "Area 2"},
    {5, 2, P_SOUTH2, "South Pole", "Area 3"},
    {5, 3, P_SOUTH2, "South Pole", "Pete's Ambush"},
    {6, 0, P_INDIA, "Maharajah", "Palace Grounds"},
    {6, 8, P_INDIA, "Maharajah", "Palace Gate"},
    {6, 1, P_INDIA, "Maharajah", "Palace Room 1"},
    {6, 2, P_INDIA, "Maharajah", "Palace Room 2"},
    {6, 3, P_INDIA, "Maharajah", "Palace Room 3"},
    {6, 4, P_INDIA, "Maharajah", "Palace Room 4"},
    {6, 5, P_INDIA, "Maharajah", "Palace Room 5"},
    {6, 6, P_INDIA, "Maharajah", "Palace Room 6"},
    {6, 7, P_INDIA, "Maharajah", "Boss - The Tiger"},
    {7, 0, P_EGYPT, "Egypt", "Desert & Sphinx"},
    {7, 1, P_EGYPT, "Egypt", "Pyramid 1"},
    {7, 2, P_EGYPT, "Egypt", "Pyramid 2"},
    {7, 3, P_EGYPT, "Egypt", "Pyramid 3"},
    {7, 4, P_EGYPT, "Egypt", "Pyramid 4"},
    {7, 5, P_EGYPT, "Egypt", "Mine Cart"},
    {8, 0, P_HIDE, "Pete's Hideout", "Entrance"},
    {8, 1, P_HIDE, "Pete's Hideout", "Room 1"},
    {8, 2, P_HIDE, "Pete's Hideout", "Room 2"},
    {8, 3, P_HIDE, "Pete's Hideout", "Room 3"},
    {8, 4, P_HIDE, "Pete's Hideout", "Boss - Pete"},
    {9, 0, P_ISLAND, "The Island", "Area 1"},
    {9, 1, P_ISLAND, "The Island", "Area 2"},
    {9, 2, P_ISLAND, "The Island", "Area 3"},
    {9, 3, P_ISLAND, "The Island", "Final Boss"},
    {0, 0, P_NEW, "Ending", "The Great Duck Treasure"},
};
const int kReturnEntry[10] = {-1, 0, 2, 0, 2, 0, 0, 0, -1, -1};   // ROM $475C

const SoundEntry kSounds[] = {
    {0x81, "Title", true}, {0x90, "Story & Dialogue", true}, {0x92, "World Map", true},
    {0x83, "Duckburg", true}, {0x8F, "Gyro's Workshop", true}, {0x84, "Mexico", true},
    {0x8B, "Ruins & Dungeons", true}, {0x88, "Transylvania", true}, {0x82, "Boss Battle", true},
    {0x95, "Item Get", true}, {0x85, "Maharajah - Grounds", true}, {0x87, "Maharajah - Palace", true},
    {0x86, "Egypt - Desert", true}, {0x89, "South Pole", true}, {0x8E, "Viking Ship", true},
    {0x8A, "Pete's Hideout", true}, {0x96, "Hideout Depths", true}, {0x94, "Final Boss", true},
    {0x93, "The Great Duck Treasure", true}, {0x8C, "Ending", true}, {0x8D, "Staff Roll", true},
    {0x91, "Game Over", true}, {0x97, "Continue?", true}, {0x98, "Unused Theme", true},
    {0xA0, "Jump", false}, {0xA1, "Plunger Shot", false}, {0xA2, "Plunger Sticks", false},
    {0xA3, "Plunger Bounces", false}, {0xA4, "Popcorn Shot", false}, {0xA5, "Plunger Hits Enemy", false},
    {0xA6, "Bubble Gum Shot", false}, {0xA8, "Out of Ammo", false}, {0xAD, "Jump 2", false},
    {0xAE, "Donald Hurt", false}, {0xAF, "Pause", false}, {0xB0, "Item / 1-Up", false},
    {0xB1, "Airplane Departs", false}, {0xB2, "Menu Cursor", false}, {0xB4, "Airplane Arrives", false},
    {0xB8, "Enemy Defeated", false}, {0xC1, "Door Opens", false}, {0xC2, "Item Appears", false},
    {0xC7, "Jump 3", false}, {0xD5, "Ice Melts", false}, {0xE2, "Map in Water", false},
    {0xF0, "Quack Attack", false},
};

// Cheats (docs section 4.2)
std::vector<RomPatch> inf_lives_patches(const Rev& R) { return {{R.at(0x00481E), {0x5378, 0x8F98}, {0x4A78, 0x8F98}}}; }
std::vector<RomPatch> inf_health_patches(const Rev& R) {
    return {{R.at(0x005E16), {0x9F78, 0x8F96, 0x5378, 0x8F96}, {0x4E71, 0x4E71, 0x4E71, 0x4E71}}};
}
std::vector<RomPatch> invincible_patches(const Rev& R) {
    return {
        {R.at(0x00742C), {0x50F8, 0xEE09}, {0x4A38, 0xEE09}}, {R.at(0x00B866), {0x50F8, 0xEE09}, {0x4A38, 0xEE09}},
        {R.at(0x00F414), {0x50F8, 0xEE09}, {0x4A38, 0xEE09}},
    };
}
std::vector<RomPatch> inf_ammo_patches(const Rev& R) {
    return {
        {R.at(0x005108), {0x5B78, 0x8F90}, {0x4E71, 0x4E71}}, {R.at(0x005130), {0x5378, 0x8F92}, {0x4E71, 0x4E71}},
    };
}
std::vector<RomPatch> endless_quack_patches(const Rev& R) { return {{R.at(0x005FDE), {0x5378, 0x8FA8}, {0x4E71, 0x4E71}}}; }

// ---- widescreen (docs/re_qs_wide.md) --------------------------------------------------------------
// Keeps map columns [X-96, X+416) in the 512-px planes instead of [X, X+512), so both sides of a
// widened view are valid (same bytes for any E <= 80). Applied in 4:3 too: it only changes which
// off-screen columns the planes hold, so the picture and the game are unchanged, and switching
// widescreen on mid-level (or loading a 4:3 save state in widescreen) finds the columns it needs.
std::vector<RomPatch> stream_patches(const Rev& R) {
    const uint16_t off = 96;
    return {
        {R.at(0x0026C8), {0x6D0E, 0x3029, 0x0024, 0x3229, 0x0020, 0x4EBA, 0x00B2, 0x6010,
                    0x3029, 0x002C, 0x0640, 0x0200, 0x3229, 0x0020, 0x4EBA, 0x00A0},
                   {0x6D06, 0x3029, 0x0024, 0x6008, 0x3029, 0x002C, 0x0640, 0x0200,
                    0x0440, off, 0x6A02, 0x7000, 0x3229, 0x0020, 0x4EBA, 0x00A0}},
        {R.at(0x0026FC), {0x6D0C, 0x3029, 0x0024, 0x3229, 0x0020, 0x6000, 0x0014,
                    0x3029, 0x0024, 0x3229, 0x0028, 0x0641, 0x0100, 0x6000, 0x0004},
                   {0x6D06, 0x3229, 0x0020, 0x6008, 0x3229, 0x0028, 0x0641, 0x0100,
                    0x3029, 0x0024, 0x0440, off, 0x6A02, 0x7000, 0x6002}},
        {R.at(0x003FCE), {0x4EBA, R.stream_call_old}, {0x4EBA, R.stream_call_new}},
    };
}

// The "spawns behind the player" option (see wide_patches): the enemy types it moves, how far beyond the wide view's left
// edge (half a sprite's width), and the object field ($3E, unused by these types) that holds how far an object was moved,
// which post_frame() makes up while it flies or walks in, so it acts where it would at 4:3.
// swoop-in birds, the golden Scrooge statues, Egypt's guards, India's snake charmers (the one of a pair made behind
// Donald, inside the 4:3 view; its basket $53 follows it by itself)
constexpr uint16_t kBehindTypes[] = {0x0D, 0x7C, 0x4B, 0xB0, 0x79, 0x52};
constexpr uint16_t kBehindRight = 0x79;   // ... this one comes up behind Donald on the right, as he walks left
constexpr int kBehindMargin = 24;
constexpr uint16_t kCatchUpField = 0x3E;
constexpr uint16_t kDedupType = 0x4B;   // penguins: an entry is not made while a live object of its twin entry exists
constexpr unsigned kCharmerExtra = 5;   // a snake charmer's steps clear of the terrain after catching up (55 px)
// The statues ($B0) come in groups: they are all moved by the same distance, so they keep their spacing - the most any of
// them needs, with kStatueReach the furthest right (screen x) one is moved from.
constexpr int kStatueReach = 96;

// A few words of 68000 code with short forward/backward branches to labels (the 8-bit displacement of Bcc.s / BSR.s)
struct Asm {
    std::vector<uint16_t> w;
    std::map<std::string, size_t> at;
    std::vector<std::pair<size_t, std::string>> fixups;
    void put(std::initializer_list<uint16_t> v) { w.insert(w.end(), v.begin(), v.end()); }
    void br(uint16_t op, const char* to) { fixups.push_back({w.size(), to}); w.push_back(op); }
    void label(const char* n) { at[n] = w.size(); }
    std::vector<uint16_t> finish() {
        for (const auto& f : fixups) {
            const int d = 2 * ((int)at.at(f.second) - (int)f.first - 1);
            if (d < -128 || d > 126 || d == 0) w.clear();   // out of a short branch's reach: no patch (patches_safe fails)
            else w[f.first] |= (uint8_t)d;
        }
        return w;
    }
};

// `fix`: the "enemies that spawn behind the player" option (GameConfig key "spawn_fix"): see the block below
std::vector<RomPatch> wide_patches(const Rev& R, int E, bool fix) {
    // Objects are kept alive in world X [camX-256, camX+512); from 2:1 on (right edge camX+384) the
    // right spawn trigger also looks one 128-px cell further so enemies don't appear at the edge.
    const bool far = E >= kFarFrom;
    // Added code goes to free ROM (R.code); the jumps into the game's own code use this release's addresses.
    const uint32_t base = R.code;
    auto hi = [](uint32_t a) { return (uint16_t)(a >> 16); };
    auto lo = [](uint32_t a) { return (uint16_t)a; };
    const uint32_t spawn_col = R.at(0x019C20);   // the spawner's column entry (the "jsr $19C20" below)
    const uint16_t keep = far ? 0x0380 : 0x0300;
    std::vector<RomPatch> p = {
        // sprite piece clip: $60-E .. $1C0+E
        // (unsigned tests: the left bound must stay above 0 - beyond E = 95 pieces starting left of VDP X 1
        // are dropped, at most 20 px at the edge)
        {R.at(0x009BCE), {0x0C40, 0x0060}, {0x0C40, (uint16_t)std::max(1, 0x60 - E)}},
        {R.at(0x009BD4), {0x0C40, 0x01C0}, {0x0C40, (uint16_t)(0x1C0 + E)}},
        // objects: spawn one 128-px cell further left, despawn 256 px off the left edge (the left column is
        // in the trigger code at TRIGGER, below)
        {R.at(0x019D74), {0x0C47, 0x0280}, {0x0C47, keep}},
        {R.at(0x009A9A), {0x0C44, 0x0280, 0x6400, R.despawn_far, 0x4A44, 0x6B00, R.despawn_left},
                         {0x3004, 0x0640, 0x0080, 0x0C40, keep, 0x6400, R.despawn_left}},
    };
    // Objects that leave and re-enter by the old screen edges, moved E px out so they do it outside the
    // widened view: $3D/$48/$B3 (Hideout bushes and kin) wrap from camX+352 to camX-32; $4F parks at
    // camX+352 for a while before flying in.
    const uint16_t wrap_r = (uint16_t)(0x160 + E), wrap_l = (uint16_t)(0x20 + E);
    for (uint32_t a : {0x010FE4u, 0x011032u, 0x01107Eu, 0x012164u}) p.push_back({R.at(a), {0x0640, 0x0160}, {0x0640, wrap_r}});
    for (uint32_t a : {0x010FF4u, 0x011042u, 0x01108Eu}) p.push_back({R.at(a), {0x0468, 0x0020, 0x0010}, {0x0468, wrap_l, 0x0010}});
    p.push_back({R.at(0x010F12), {0x0469, 0x0020, 0x0010}, {0x0469, wrap_l, 0x0010}});
    p.push_back({R.at(0x0110FA), {0x0440, 0x0020}, {0x0440, wrap_l}});
    // Duckburg's Mickey blimp ($17, a background sprite): it waits hidden at camera X - $1C ($D7D4, just past the 4:3
    // left edge), then crosses the sky from there or from camera X - $1C + $178 = +348 (the right edge), and hides again
    // when the on-screen test says it has left ($D76E, made wide below). Both starts move out by E, so it comes in from
    // beyond the wide view's edges instead of appearing (and vanishing) inside it.
    p.push_back({R.at(0x00D7DA), {0x0468, 0x001C, 0x0010}, {0x0468, (uint16_t)(0x1C + E), 0x0010}});
    p.push_back({R.at(0x00D7F6), {0x0668, 0x0178, 0x0010}, {0x0668, (uint16_t)(0x178 + 2 * E), 0x0010}});
    p.push_back({R.at(0x011FEC), {0x0C41, 0x0160}, {0x0C41, wrap_r}});
    p.push_back({R.at(0x011FF4), {0x0C41, 0x0140}, {0x0C41, (uint16_t)(0x140 + E)}});
    // Old "far off screen" limits that a wide view reaches: player shots are deleted outside
    // [camX-64, camX+384) ($5956); the type $19 walkers stop beyond screen x 416 / -96, and another walker
    // ($21/$55/$B0) stands still beyond -64 / 384 before turning back. Moved out by E-64: shots go to the
    // edges, the $19 walkers (which wait in plain sight) stay 32 px (a sprite's width) beyond them at 2:1
    // and 21:9. The other walker's limits only move at 21:9 (no visible case; moving them out at 2:1 too
    // changed a $B0's behaviour in The Island Area 1).
    if (E > 64) {
        const uint16_t d = (uint16_t)(E - 64);
        p.push_back({R.at(0x005976), {0x0441, 0x0040}, {0x0441, (uint16_t)(0x40 + d)}});
        p.push_back({R.at(0x00597E), {0x0641, 0x01C0}, {0x0641, (uint16_t)(0x1C0 + 2 * d)}});
        for (uint32_t a : {0x00CD52u, 0x00CE92u}) p.push_back({R.at(a), {0x0C40, 0x01A0}, {0x0C40, (uint16_t)(0x1A0 + d)}});
        for (uint32_t a : {0x00CDB0u, 0x00CF02u}) p.push_back({R.at(a), {0x0C40, 0xFFA0}, {0x0C40, (uint16_t)(-0x60 - d)}});
        if (!fix) p.push_back({R.at(0x013A6E), {0x0C40, 0xFFC0}, {0x0C40, (uint16_t)(-0x40 - d)}});
        p.push_back({R.at(0x013AD6), {0x0C40, 0x0180}, {0x0C40, (uint16_t)(0x180 + d)}});
    }
    // (with the "spawns behind the player" option the walkers' left limit moves out beyond where the option puts the
    // golden Scrooge statues, so that they walk in from there - see that block)
    if (fix) p.push_back({R.at(0x013A6E), {0x0C40, 0xFFC0}, {0x0C40, (uint16_t)(-0x40 - (kStatueReach + E + kBehindMargin))}});
    if (far) {
        // Right trigger ($19C08, camera X crossing a 128-px cell boundary to the right): the original column
        // (X+$180)>>7 and the far one (X+$200)>>7 both run (wrapper at $7B400). Spawn entries are filed by
        // cell, but some sit cells to the left of theirs (birds that swoop in behind Donald); the far pass
        // would create those in the middle of the view. Entries more than 192 px left of their cell (at 4:3
        // they appear in the left half of the screen or off its left edge) are "deferred": the far pass
        // leaves them and the original-column pass, 128 px later, creates only them - at the 4:3 moment,
        // so also at the same distance from Donald. Entries only a little left of their cell keep the far
        // timing, which brings them in at the right edge as 4:3 does. Entry hook at $7B420.
        // The Island's vines ($87, Area 1's climbing shaft) are deferred too: a vine crumbles once Donald is 64 px
        // above it, so created a cell early - while he is still coming down the tunnels above the shaft - the
        // bottom ones fell away before he got there and the climb could not be started.
        // The two passes are told apart by the wrapper's return addresses ($7B40C original, $7B418 far), which
        // nothing else pushes - the area-start spawner ($19D5A) and the left trigger use the same loop.
        const std::vector<uint16_t> wrap = {
            0x3F00, 0x0640, 0x0180, 0x4EB9, hi(spawn_col), lo(spawn_col),   // move.w d0,-(sp) / addi.w #$180,d0 / jsr $19C20
            0x301F, 0x0640, 0x0200, 0x4EB9, hi(spawn_col), lo(spawn_col),   // move.w (sp)+,d0 / addi.w #$200,d0 / jsr $19C20
            0x4E75,                                           // rts
        };
        p.push_back({base + WRAPPER_AT, std::vector<uint16_t>(wrap.size(), 0xFFFF), wrap});
    }
    {
        // Entry hook (HOOK_AT), in $19C5E's entry loop before the free-slot search: a2 = entry+1 (x at 6(a2)), 6(sp) =
        // column (saved d2), 20(sp) = $19C52 when called from the column loop, 24(sp) = that loop's caller. d3/d6 are
        // free here. The passes are told apart by their return addresses (the wrapper's, the left trigger's), which
        // nothing else pushes - the area-start spawner ($19D5A) uses the same loop.
        // The vertical trigger's extra column (X>>7)+4 (`$19BEA` below) leaves deferred entries too: 4:3 never
        // creates them from there, and they would appear inside the view.
        // Left trigger (TRIGGER below): the mirror image. The 4:3 column (X-$71)>>7 and the wide one a cell further
        // left both run. Entries filed more than 256 px right of their cell's start (Egypt's guards $79 that come
        // up behind Donald as he walks left: at 4:3 they are made off the right edge) are deferred to the 4:3
        // column's pass - created a cell early they stood in the middle of the view.
        const uint32_t col_loop = R.at(0x019C52), vert_trigger = R.at(0x019BFC), free_slot = R.at(0x019D7C);
        const uint32_t orig_pass = base + WRAPPER_AT + 0x0C, far_pass = base + WRAPPER_AT + 0x18;   // $7B40C / $7B418 in REV 00
        const uint32_t left_orig = R.trigger() + 0x10, left_far = R.trigger() + 0x1C;
        Asm h;
        h.br(0x6000, "start");
        h.label("all");  h.put({(uint16_t)(far ? 0x7C00 : 0x7C01), 0x4E75});   // SPAWN_ALL_FLAG: moveq #0/1,d6 / rts (no far pass: all)
        h.label("lall"); h.put({0x7C00, 0x4E75});               // LEFT_FLAG:      moveq #0/1,d6 / rts
        h.label("start");
        h.put({0x0CAF, hi(col_loop), lo(col_loop), 0x0014});    // cmpi.l #$19C52,20(sp)   column loop?
        h.br(0x6700, "loop");
        h.put({0x0CAF, hi(vert_trigger), lo(vert_trigger), 0x0014});   // cmpi.l #$19BFC,20(sp)   vertical trigger?
        h.br(0x6600, "go");
        h.put({0x3C2C, 0x0024, 0xEE46, 0x5846,                  // move.w $24(a4),d6 / asr.w #7,d6 / addq.w #4,d6
               0xBC6F, 0x0006});                                // cmp.w 6(sp),d6          its extra column?
        h.br(0x6600, "go");
        h.label("far");
        h.br(0x6100, "deferred"); h.br(0x6B00, "skip");         // bsr deferred / bmi skip
        h.label("go");
        {
            // (the free slot search is reached through DEDUP_AT, the penguins' twin check)
            const uint32_t next = base + DEDUP_AT;
            h.put({0x47F8, 0x9000, 0x3C3C, 0x002F,              // lea $9000.w,a3 / move.w #$2f,d6 (as the original)
                   0x4EF9, hi(next), lo(next)});                // jmp $19D7C (free slot search, carry = none)
        }
        h.label("skip");
        h.put({0x003C, 0x0001, 0x4E75});                        // ori #1,ccr / rts
        h.label("loop");
        h.put({0x0CAF, hi(orig_pass), lo(orig_pass), 0x0018});  // cmpi.l #$7B40C,24(sp)   right: original-column pass?
        h.br(0x6700, "orig");
        h.put({0x0CAF, hi(far_pass), lo(far_pass), 0x0018});    // cmpi.l #$7B418,24(sp)   right: far pass?
        h.br(0x6700, "far");
        h.put({0x0CAF, hi(left_orig), lo(left_orig), 0x0018});  // cmpi.l #..,24(sp)       left: 4:3 column pass?
        h.br(0x6700, "lorig");
        h.put({0x0CAF, hi(left_far), lo(left_far), 0x0018});    // cmpi.l #..,24(sp)       left: wide column pass?
        h.br(0x6600, "go");
        h.br(0x6100, "rdeferred"); h.br(0x6B00, "skip"); h.br(0x6000, "go");
        h.label("orig");
        h.br(0x6100, "all"); h.br(0x6600, "go");                // bsr all / bne go
        h.br(0x6100, "deferred"); h.br(0x6A00, "skip"); h.br(0x6000, "go");
        h.label("lorig");
        h.br(0x6100, "lall"); h.br(0x6600, "go");
        h.br(0x6100, "rdeferred"); h.br(0x6A00, "skip"); h.br(0x6000, "go");
        h.label("deferred");                                    // < 0: more than 192 px left of its cell, or a vine
        h.put({0x0C12, 0x0087, 0x6604,                          // cmpi.b #$87,(a2) / bne.s +4
               0x7CFF, 0x4E75,                                  // moveq #-1,d6 / rts
               0x1C2A, 0x0006, 0xE14E, 0x1C2A, 0x0007,          // move.b 6(a2),d6 / lsl.w #8,d6 / move.b 7(a2),d6   entry x
               0x362F, 0x000A, 0xEF4B, 0x9C43,                  // move.w 10(sp),d3 / lsl.w #7,d3 / sub.w d3,d6
               0x0646, 0x00C0, 0x4E75});                        // addi.w #192,d6 / rts
        h.label("rdeferred");                                   // < 0: more than 256 px right of its cell's start
        h.put({0x1C2A, 0x0006, 0xE14E, 0x1C2A, 0x0007,          // entry x
               0x362F, 0x000A, 0xEF4B, 0x9646,                  // move.w 10(sp),d3 / lsl.w #7,d3 / sub.w d6,d3
               0x0643, 0x0100, 0x4E75});                        // addi.w #256,d3 / rts
        const std::vector<uint16_t> hook = h.finish();
        if (!hook.empty()) {
            // Penguins ($4B): the level files some of them twice - the same type, Y and X in two cells, one for each
            // direction Donald comes from - and the wider despawn window keeps the first one alive, so walking back and
            // forth over a cell boundary made a second (and a third...) penguin at the same spot. An entry whose twin
            // (same Y and X) already has a live object is not created now: it is skipped as if it were deferred (the
            // spawn flag stays clear), so it comes back once the first penguin has gone, as a lone one does.
            // The object's $2E points at its entry's spawned byte (entry + 3); Y is at +5 and X at +7 of the entry,
            // i.e. 2 / 4 from that byte and 4 / 6 from a2 (entry + 1); the records are byte-aligned.
            Asm t;
            t.put({0x0C12, kDedupType});                        // cmpi.b #$4B,(a2)        a penguin?
            t.br(0x6600, "fs");                                 // bne.s fs
            t.put({0x48E7, 0x0080,                              // movem.l a0,-(sp)
                   0x47F8, 0x9000, 0x3C3C, 0x002F});            // lea $9000.w,a3 / move.w #$2f,d6
            t.label("dl");
            t.put({0x0C53, kDedupType});                        // cmpi.w #$4B,(a3)        a live penguin?
            t.br(0x6600, "dn");                                 // bne.s dn
            t.put({0x306B, 0x002E,                              // movea.w $2E(a3),a0      its entry's spawned byte
                   0x162A, 0x0004, 0xB628, 0x0002});            // move.b 4(a2),d3 / cmp.b 2(a0),d3    Y high
            t.br(0x6600, "dn");
            t.put({0x162A, 0x0005, 0xB628, 0x0003});            // Y low
            t.br(0x6600, "dn");
            t.put({0x162A, 0x0006, 0xB628, 0x0004});            // X high
            t.br(0x6600, "dn");
            t.put({0x162A, 0x0007, 0xB628, 0x0005});            // X low
            t.br(0x6600, "dn");
            t.put({0x4CDF, 0x0100,                              // movem.l (sp)+,a0       a twin is alive: skip this entry
                   0x003C, 0x0001, 0x4E75});                    // ori #1,ccr / rts
            t.label("dn");
            t.put({0x47EB, 0x0040, 0x5346});                    // lea $40(a3),a3 / subq.w #1,d6
            t.br(0x6A00, "dl");                                 // bpl.s dl
            t.put({0x4CDF, 0x0100,                              // movem.l (sp)+,a0       no twin: create it
                   0x47F8, 0x9000});                            // lea $9000.w,a3
            t.label("fs");
            t.put({0x3C3C, 0x002F,                              // move.w #$2f,d6         (flags as the game's own)
                   0x4EF9, hi(free_slot), lo(free_slot)});      // jmp $19D7C (free slot search, carry = none)
            const std::vector<uint16_t> dedup = t.finish();
            if (dedup.size() * 2 <= 0x100) p.push_back({base + DEDUP_AT, std::vector<uint16_t>(dedup.size(), 0xFFFF), dedup});
        }
        if (!hook.empty()) {
            p.push_back({base + HOOK_AT, std::vector<uint16_t>(hook.size(), 0xFFFF), hook});
            p.push_back({R.at(0x019C92), {0x47F8, 0x9000, 0x3C3C, 0x002F, 0x4EBA, 0x00E0},
                                         {0x4EB9, hi(base + HOOK_AT), lo(base + HOOK_AT), 0x4E71, 0x4E71, 0x4E71}});
        }
    }
    if (far) {
        // ...and the vertical trigger spawns 6 cell columns instead of 5 ((X>>7)-1 .. +4), so objects in that
        // extra column also appear when they come into range by scrolling up or down
        p.push_back({R.at(0x019BEA), {0x7E04}, {0x7E05}});
    }
    if (fix) {
        // Option "spawns behind the player" (GameConfig key "spawn_fix", off by default). Some spawn entries are filed far
        // left of their cell, and the right trigger's original-column pass (the 4:3 one) creates them behind Donald as
        // the camera moves right: Mexico's swoop-in bird $0D just off the 4:3 left edge (x -1..-3, inside a wide view),
        // the Island's golden Scrooge statues $B0 even inside the 4:3 view. With the option on they are still created
        // then - so they act at the same moment and place relative to Donald as at 4:3 - but put just beyond the wide
        // view's left edge: x = camera X - E - kBehindMargin (the margin: half a sprite's width). Only enemy types that
        // move (the swoop-in birds $0D / $7C / $4B, the statues $B0), and only those that would otherwise be created
        // inside or at the view on its left half; everything else - platforms, vines, items - stays where it is.
        // The entry loop writes the new object's X at $19CC4 (`move.b (a2)+,$11(a3)` / `bra.s $19CCE`): that is
        // replaced by a jsr to POST_AT, which does the same, then checks the caller (as the spawn hook does: 20(sp) =
        // the column loop, 24(sp) = the wrapper's original-column return) and moves the object. Below 16:9 there is no
        // far pass: the trigger then goes through the wrapper too (the original column only), so the pass is known.
        if (!far) {
            const std::vector<uint16_t> wrap = {
                0x3F00, 0x0640, 0x0180, 0x4EB9, hi(spawn_col), lo(spawn_col),   // move.w d0,-(sp) / addi.w #$180,d0 / jsr $19C20
                0x301F, 0x4E75,                                                 // move.w (sp)+,d0 / rts
            };
            p.push_back({base + WRAPPER_AT, std::vector<uint16_t>(wrap.size(), 0xFFFF), wrap});
        }
        const uint32_t col_loop = R.at(0x019C52), orig_pass = base + WRAPPER_AT + 0x0C, left_orig = R.trigger() + 0x10;
        const uint16_t out = (uint16_t)(E + kBehindMargin);
        Asm a;
        a.put({0x175A, 0x0011,                                  // move.b (a2)+,$11(a3)       (as the game)
               0x5897,                                          // addq.l #4,(sp)             return to $19CCE (the bra.s target)
               0x48E7, 0x1200,                                  // movem.l d3/d6,-(sp)
               0x1C2B, 0x0001});                                // move.b 1(a3),d6            type
        for (uint16_t type : kBehindTypes) {
            a.put({0x0C06, type});                              // cmpi.b #type,d6
            a.br(0x6700, "type");                               // beq.s type
        }
        a.label("done");
        a.put({0x4CDF, 0x0048, 0x4E75});                        // movem.l (sp)+,d3/d6 / rts
        a.label("type");
        a.put({0x426B, kCatchUpField,                           // clr.w $3E(a3)              nothing to catch up (yet)
               0x0CAF, hi(col_loop), lo(col_loop), 0x001C});     // cmpi.l #$19C52,28(sp)      the column loop?
        a.br(0x6600, "done");
        a.put({0x3C2B, 0x0010, 0x3638, 0x8524, 0x9C43});         // move.w $10(a3),d6 / move.w $8524.w,d3 / sub.w d3,d6   screen x
        a.put({0x0CAF, hi(orig_pass), lo(orig_pass), 0x0020});   // cmpi.l #orig,32(sp)        the right trigger's original-column pass?
        a.br(0x6700, "right");
        // the left trigger's 4:3-column pass: a guard made inside the view's right half goes just beyond its right edge
        a.put({0x0CAF, hi(left_orig), lo(left_orig), 0x0020});   // cmpi.l #left,32(sp)
        a.br(0x6600, "done");
        a.put({0x0C2B, kBehindRight, 0x0001});                   // cmpi.b #$79,1(a3)
        a.br(0x6600, "done");
        a.put({0x0C46, 0x00A0});                                 // cmpi.w #160,d6
        a.br(0x6F00, "done");                                    // ble.s done
        a.put({0x0C46, (uint16_t)(320 + out)});                  // cmpi.w #320+out,d6
        a.br(0x6C00, "done");                                    // bge.s done                 (already beyond the edge)
        a.put({0x0643, (uint16_t)(320 + out), 0x3743, 0x0010,    // addi.w #320+out,d3 / move.w d3,$10(a3)
               0x4446, 0x0646, (uint16_t)(320 + out + 0x800),    // neg.w d6 / addi.w #320+out+$800,d6   distance, bit 11: to the left
               0x3746, kCatchUpField});                          // move.w d6,$3E(a3)
        a.br(0x6000, "done");
        a.label("right");
        a.put({0x0C2B, kBehindRight, 0x0001});                   // cmpi.b #$79,1(a3)
        a.br(0x6700, "end");
        a.put({0x0C46, (uint16_t)-out});                        // cmpi.w #-out,d6
        a.br(0x6F00, "end");                                   // ble.s done                 (already beyond the edge)
        a.put({0x0C2B, 0x00B0, 0x0001});                        // cmpi.b #$B0,1(a3)          a statue?
        a.br(0x6700, "statue");
        a.put({0x0C46, 0x00A0});                                // cmpi.w #160,d6
        a.br(0x6C00, "end");                                   // bge.s done                 (right half: as it is)
        a.put({0x0443, out, 0x3743, 0x0010,                     // subi.w #out,d3 / move.w d3,$10(a3)   x = camera X - out
               0x0646, out, 0x3746, kCatchUpField});            // addi.w #out,d6 / move.w d6,$3E(a3)   how far it was moved
        a.br(0x6000, "end");
        const uint16_t reach = (uint16_t)(kStatueReach + out);
        a.label("statue");
        a.put({0x0C46, (uint16_t)kStatueReach});                // cmpi.w #reach,d6
        a.br(0x6C00, "end");                                   // bge.s done
        a.put({0x046B, reach, 0x0010,                           // subi.w #reach,$10(a3)      all moved by the same distance
               0x377C, reach, kCatchUpField});                  // move.w #reach,$3E(a3)
        a.label("end");
        a.put({0x4CDF, 0x0048, 0x4E75});                        // movem.l (sp)+,d3/d6 / rts
        const std::vector<uint16_t> post = a.finish();
        if (!post.empty() && post.size() * 2 <= WAKE_AT - POST_AT) {   // (a branch out of reach: leave the game as it is)
            p.push_back({base + POST_AT, std::vector<uint16_t>(post.size(), 0xFFFF), post});
            p.push_back({R.at(0x019CC4), {0x175A, 0x0011, 0x6004}, {0x4EB9, hi(base + POST_AT), lo(base + POST_AT)}});
        }
        // A moved snake charmer $52 stands behind India's block stacks. It steps 11 px in the direction of bit 0 of $36
        // ($1013A; its basket $2A(a0) has the same bit), then the terrain probe ($158CA) lifts it onto what is under
        // its feet and reports a wall (d7 = 6 -> $1018A: the step undone, the direction flipped). So it climbed onto
        // the blocks, stayed up there and paced to and fro. While $3E is not 0 - catching up, then a few more steps
        // (post_frame) - it faces right (CHARM_AT) and the probe is skipped (CWALL_AT): it walks through at the
        // height it was made at, as it would never have been there at 4:3.
        const uint32_t wall = R.at(0x01018A), probe = R.at(0x0158CA);
        const std::vector<uint16_t> charm = {
            0x4A68, kCatchUpField, 0x670C,                       // tst.w $3E(a0) / beq.s plain
            0x08A8, 0x0000, 0x0036,                              // bclr #0,$36(a0)
            0x3268, 0x002A, 0x08A9, 0x0000, 0x0036,              // move.w $2A(a0),a1 / bclr #0,$36(a1)
            0x0828, 0x0000, 0x0036, 0x4E75,                      // plain: btst #0,$36(a0) / rts   (as the game)
        };
        const std::vector<uint16_t> cwall = {
            0x4A68, kCatchUpField, 0x6612,                       // tst.w $3E(a0) / bne.s ret
            0x4EB9, hi(probe), lo(probe),                        // jsr $158CA          (as the game)
            0x0C47, 0x0006, 0x6606,                              // cmpi.w #6,d7 / bne.s ret
            0x4EF9, hi(wall), lo(wall),                          // jmp $1018A
            0x4E75,                                              // ret: rts
        };
        p.push_back({base + CHARM_AT, std::vector<uint16_t>(charm.size(), 0xFFFF), charm});
        p.push_back({base + CWALL_AT, std::vector<uint16_t>(cwall.size(), 0xFFFF), cwall});
        p.push_back({R.at(0x01013A), {0x0828, 0x0000, 0x0036}, {0x4EB9, hi(base + CHARM_AT), lo(base + CHARM_AT)}});
        // (bsr $158CA: its displacement differs between the releases)
        for (uint32_t at : {0x010148u, 0x01015Au}) {
            const uint32_t here = R.at(at);
            p.push_back({here, {0x4EBA, (uint16_t)(probe - (here + 2)), 0x0C47, 0x0006, (uint16_t)(at == 0x010148u ? 0x6738 : 0x6726)},
                               {0x4EF9, hi(base + CWALL_AT), lo(base + CWALL_AT), 0x4E71, 0x4E71}});
        }
    }
    {
        // The golden Scrooge statues $B0, asleep (state 1, $ECC4): they only test Donald's distance and wake within
        // 120 px ($78 / -$78) - shots fly through them, which shows on a wide view, where they stand in sight long
        // before that. The state now runs the shot test the other states use ($B8CE: a1 = the shot, d7 != 0) and
        // hands the shot the statue as the awake ones do (shot $30 = statue), so a plunger bounces off and drops.
        // $0F (the test's result, which the awake states act on) is cleared again: the statue stays asleep. Its
        // sleeping frame has no hit box ($25 = 0, an index into the box table), so it borrows the walking one's ($C7)
        // for the test.
        // With the spawn option the distance is measured from where a moved statue will be ($3E px further on): it
        // wakes at the moment and place it does at 4:3 and walks in from beyond the view's edge, like the birds.
        const uint32_t shot_test = R.at(0x00B8CE);
        std::vector<uint16_t> wake = {
            0x117C, 0x00C7, 0x0025,                              // move.b #$C7,$25(a0)   the walking statue's hit box
            0x4EB9, hi(shot_test), lo(shot_test),                // jsr $B8CE
            0x670A,                                              // beq.s none
            0x3348, 0x0030, 0x08E8, 0x0004, 0x000E,              // move.w a0,$30(a1) / bset #4,$E(a0)
            0x4228, 0x000F, 0x4228, 0x0025,                      // none: clr.b $F(a0) / clr.b $25(a0)
            0x3038, 0xA2D0, 0x9068, 0x0010,                      // move.w $A2D0.w,d0 / sub.w $10(a0),d0   (as the game)
        };
        if (fix) wake.insert(wake.end(), {0x2F01, 0x3228, kCatchUpField, 0x0241, 0x07FF,   // move.l d1,-(sp) / move.w $3E(a0),d1 / andi.w #$7FF,d1
                                          0x9041, 0x221F});                                 // sub.w d1,d0 / move.l (sp)+,d1
        wake.push_back(0x4E75);
        p.push_back({base + WAKE_AT, std::vector<uint16_t>(wake.size(), 0xFFFF), wake});
        p.push_back({R.at(0x00ECC4), {0x3038, 0xA2D0, 0x9068, 0x0010}, {0x4EB9, hi(base + WAKE_AT), lo(base + WAKE_AT), 0x4E71}});
    }
    // Column trigger ($19C08, the camera crossed a 128-px boundary; d2 = its X step): `tst.w d2 / bmi.s` at $19C12
    // now goes to TRIGGER. Left: the column one cell further left, (X-$F1)>>7, so objects appear before the widened
    // left edge. Right: the far wrapper ($7B400) from 2:1 on, else the original (X+$180)>>7.
    // Turning round, the widened passes skip a column 4:3 creates at once: moving left, the 4:3 column's pass makes
    // only deferred entries, assuming the wide pass made the rest a cell earlier; moving right from 16:9 on, the same
    // for the original-column pass. Entries that have meanwhile gone (deleted off screen, like the Egypt bridge
    // pieces $75/$8E) or that only appear on the way back are then missing until the camera crosses back.
    // post_frame() sees each crossing a frame before the spawner does and, for the first crossing after a turn,
    // makes that pass create all its entries (LEFT_FLAG / SPAWN_ALL_FLAG) - so that crossing creates what 4:3 creates.
    {
        std::vector<uint16_t> t = {
            0x4A42, 0x6A1A,                   // 00 tst.w d2 / bpl.s right (1E)
            0x3F00, 0x0440, 0x0071,           // 04 move.w d0,-(sp) / subi.w #$71,d0      the 4:3 column
            0x4EB9, hi(spawn_col), lo(spawn_col),   // 0A jsr $19C20                      (returns to 10)
            0x301F, 0x0440, 0x00F1,           // 10 move.w (sp)+,d0 / subi.w #$F1,d0      one cell further
            0x4EB9, hi(spawn_col), lo(spawn_col),   // 16 jsr $19C20                      (returns to 1C)
            0x4E75};                          // 1C rts
        if (far || fix) t.insert(t.end(), {0x4EF9, hi(base + WRAPPER_AT), lo(base + WRAPPER_AT)});   // 1E right: jmp wrapper
        else t.insert(t.end(), {0x0640, 0x0180, 0x4EF9, hi(spawn_col), lo(spawn_col)});   // 1E addi.w #$180,d0 / jmp
        p.push_back({R.trigger(), std::vector<uint16_t>(t.size(), 0xFFFF), t});
        p.push_back({R.at(0x019C14), {0x6B06, 0x0640, 0x0180, 0x6004, 0x0440, 0x0071},
                                     {0x4E71, 0x4EF9, hi(R.trigger()), lo(R.trigger()), 0x4E71, 0x4E71}});
    }
    // Camera clamp: keep plane A's camera E px inside its bounds when the area is wide enough,
    // so the widened view never shows outside the level (code in the unused gap at $7B000).
    // An area narrower than the view but wider than 320 px (the 512-px rooms at 21:9) gets its camera
    // held at the centre, so the few columns beyond the room are split evenly over both sides.
    const uint16_t need = (uint16_t)(2 * E + 2), e = (uint16_t)E;
    std::vector<uint16_t> code = {
        0x2229, 0x0004, 0x2411, 0xB2FC, 0x8500, 0x6618, 0x2602, 0x9681, 0x0C83, need, 0x0000, 0x6D2C,
        0x0681, e, 0x0000, 0x0482, e, 0x0000, 0xB2A9, 0x0024, 0x6C0C, 0xB4A9, 0x0024, 0x6E12, 0x4A80,
        0x6B0E, 0x6004, 0x4A80, 0x6E08, 0x91A9, 0x0024, 0x42A9, 0x0034, 0x4E75,
        // narrow (range > 0): X = lo + range/2, velocity 0
        0x4A83, 0x67DC, 0xE283, 0xD283, 0x2341, 0x0024, 0x42A9, 0x0034, 0x4E75,
        // $7B056: area load
        0x4EB9, hi(R.at(0x5000)), lo(R.at(0x5000)), 0x43F8, 0x8500, 0x2229, 0x0004, 0x2411, 0x2602, 0x9681, 0x0C83, need,
        0x0000, 0x6D00, 0x006E, 0x0681, e, 0x0000, 0x0482, e, 0x0000, 0x2029, 0x0024, 0xB081, 0x6E0A,
        0x2001, 0x0680, 0x0001, 0x0000, 0x600E, 0xB082, 0x6D00, 0x0044, 0x2002, 0x0480, 0x0001, 0x0000,
        0x2200, 0x92A9, 0x0024, 0x2340, 0x0024, 0x2340, 0x002C, 0x21C1, 0x8534, 0x42B8, 0x8530, 0x43F8,
        0x8540, 0x4EB9, hi(R.at(0x25E0)), lo(R.at(0x25E0)), 0x2038, 0x8574, 0xD1B8, 0x8564, 0x21F8, 0x8564, 0x856C, 0x42B8,
        0x8574, 0x42B8, 0x8570, 0x42B8, 0x8534, 0x4EF9, hi(R.at(0x3F96)), lo(R.at(0x3F96)),
        // narrow: start at the centre (range 0 = 320-px room: unchanged)
        0x4A83, 0x67F6, 0xE283, 0x2001, 0xD083, 0x60B4,
    };
    p.push_back({base + CLAMP_AT, std::vector<uint16_t>(code.size(), 0xFFFF), code});
    p.push_back({R.at(0x002664), {0x2229, 0x0004, 0xB2A9}, {0x4EB9, hi(base + CLAMP_AT), lo(base + CLAMP_AT)}});
    p.push_back({R.at(0x00266A), {0x0024}, {0x6018}});
    p.push_back({R.at(0x0035A0), {0x4EBA, R.call_5000, 0x4EBA, R.call_3f96}, {0x4EB9, hi(base + 0x56), lo(base + 0x56), 0x4E71}});   // $7B056: area load

    // "On screen" test $E86E (screen x in (-32, 352)): objects that show themselves or are deleted by it use
    // the widened view instead, (-32-E, 352+E) - picked by the caller's return address. Otherwise the
    // Hideout bridges (and their kin) appear piece by piece inside the view, and falling things and
    // dropped items vanish there. Callers that only start an enemy's behaviour keep the original test.
    const uint32_t kWideCallers[] = {
        0x00FB7A,   // bridge / platform pieces $75 $78 $86 $8E: hidden until on screen
        0x00E6EC,   // $0A falls from the top, hides and resets once off screen
        0x00D772,   // $17 the Duckburg blimp: hides once off screen (see its starts above)
        0x00DB60, 0x00DCD0, 0x010832, 0x012258, 0x013F74,   // deleted once off screen
    };
    // $10 the garbage can worms stand still while their head is off screen. A worm leaps from its can to the next one,
    // 176 px away, so with the view's own bounds one by the edge froze stretched out in mid-air, its body in view
    // (4:3 does that too, off screen). These callers get 192 px more on both sides.
    const uint32_t kWormCallers[] = {0x00D308, 0x00D340, 0x00D444, 0x00D4F6};
    std::vector<uint16_t> on = {0x3028, 0x0010, 0x3228, 0x0014, 0x9078, 0x8524};
    std::vector<size_t> to_wide;
    for (uint32_t r : kWideCallers) {
        on.insert(on.end(), {0x0C97, hi(R.at(r)), lo(R.at(r)), 0x6700});   // cmpi.l #r,(a7) / beq.s wide
        to_wide.push_back(on.size() - 1);
    }
    std::vector<size_t> to_worm;
    for (uint32_t r : kWormCallers) {
        on.insert(on.end(), {0x0C97, hi(R.at(r)), lo(R.at(r)), 0x6700});
        to_worm.push_back(on.size() - 1);
    }
    std::vector<size_t> to_off;
    auto bound = [&](uint16_t lo, uint16_t hi, uint16_t reg) {   // cmpi.w #lo / ble.s off / cmpi.w #hi / bge.s off
        on.insert(on.end(), {(uint16_t)(0x0C40 | reg), lo, 0x6F00});
        to_off.push_back(on.size() - 1);
        on.insert(on.end(), {(uint16_t)(0x0C40 | reg), hi, 0x6C00});
        to_off.push_back(on.size() - 1);
    };
    bound(0xFFE0, 0x0160, 0);
    on.push_back(0x6000);   // bra.s ytest
    const size_t to_y = on.size() - 1;
    const size_t wide = on.size();
    bound((uint16_t)(-32 - E), (uint16_t)(352 + E), 0);
    on.push_back(0x6000);   // bra.s ytest
    const size_t to_y2 = on.size() - 1;
    const size_t worm = on.size();
    bound((uint16_t)(-224 - E), (uint16_t)(544 + E), 0);
    const size_t ytest = on.size();
    on.insert(on.end(), {0x9278, 0x8520});
    bound(0xFFF0, 0x00E0, 1);
    on.insert(on.end(), {0x7000, 0x4E75});
    const size_t off = on.size();
    on.insert(on.end(), {0x7001, 0x4E75});
    auto branch = [&](size_t at, size_t target) { on[at] |= (uint8_t)(2 * ((int)target - (int)at - 1)); };
    for (size_t at : to_wide) branch(at, wide);
    for (size_t at : to_worm) branch(at, worm);
    branch(to_y2, ytest);
    for (size_t at : to_off) branch(at, off);
    branch(to_y, ytest);
    const uint32_t ON_SCREEN = base + ON_SCREEN_AT;
    p.push_back({ON_SCREEN, std::vector<uint16_t>(on.size(), 0xFFFF), on});
    p.push_back({R.at(0x00E86E), {0x3028, 0x0010, 0x3228}, {0x4EF9, hi(ON_SCREEN), lo(ON_SCREEN)}});
    // The electric spark $08 on Duckburg's power lines (Gyro's workshop): it only shows (and buzzes) while Donald is within
    // 128 px of it ($E834: Donald X - its X, against $80 / -$80), so at 4:3 it covers the view and on a wide one it popped
    // in well inside it. The distance grows by twice the extra width and 32 px, so it reaches beyond both edges of the
    // view also while the camera leads Donald.
    const uint16_t spark = (uint16_t)(0x80 + 2 * E + 32);
    p.push_back({R.at(0x00E834), {0x9068, 0x0010, 0x0C40, 0x0080, 0x6E20, 0x0C40, 0xFF80},
                 {0x9068, 0x0010, 0x0C40, spark, 0x6E20, 0x0C40, (uint16_t)-spark}});
    // The attract intro. d: E, even (the sprites below move 2 px a frame).
    // - The treasure pile $3A at the end of the cast scroll: Donald's object creates it when his timer ($38) is at $40
    //   ($B0C8), at x $1E0 = just off the 4:3 edge ($B5DE), and it moves in for $40 frames ($B5F2) with the scroll, 1 px
    //   a frame. All three grow by E: it is made earlier, off the wide view's edge, and ends up in the same place.
    // - The title: Donald $36 walks in from x $40 for $3C frames ($B35E / $B37E), the plane $38 flies in from x $200
    //   for $90 frames ($B480 / $B4A6). They start d further out and take d/2 frames longer.
    {
        const uint16_t d = (uint16_t)((E + 1) & ~1);
        p.push_back({R.at(0x00B0C8), {0x0C68, 0x0040, 0x0038}, {0x0C68, (uint16_t)(0x40 + E), 0x0038}});
        p.push_back({R.at(0x00B5DE), {0x317C, 0x01E0, 0x0010}, {0x317C, (uint16_t)(0x1E0 + E), 0x0010}});
        p.push_back({R.at(0x00B5F2), {0x317C, 0x0040, 0x0038}, {0x317C, (uint16_t)(0x40 + E), 0x0038}});
        p.push_back({R.at(0x00B35E), {0x317C, 0x0040, 0x0010}, {0x317C, (uint16_t)(0x40 - d), 0x0010}});
        p.push_back({R.at(0x00B37E), {0x317C, 0x003C, 0x0038}, {0x317C, (uint16_t)(0x3C + d / 2), 0x0038}});
        p.push_back({R.at(0x00B480), {0x317C, 0x0200, 0x0010}, {0x317C, (uint16_t)(0x200 + d), 0x0010}});
        p.push_back({R.at(0x00B4A6), {0x317C, 0x0090, 0x0038}, {0x317C, (uint16_t)(0x90 + d / 2), 0x0038}});
    }
    // The map screen's airplane $B1: it starts at x -32 ($11918) and flies 2 px a frame ($11924) for the 180 frames the
    // screen stays ($9D66, timer $A346), so at 4:3 it is cut off at both ends (the sprite spans x-60 .. x+43). Wide, it
    // comes in from beyond the left edge and leaves completely past the right one: it starts at -E-48, flies 3 px a
    // frame and the screen stays until its tail is past 320+E (16:9: 172 frames). The map itself stays centred.
    p.push_back({R.at(0x011918), {0x317C, 0xFFE0, 0x0010, 0x317C, 0x0090, 0x0014, 0x317C, 0x0002, 0x0018},
                 {0x317C, (uint16_t)(-48 - E), 0x0010, 0x317C, 0x0090, 0x0014, 0x317C, 0x0003, 0x0018}});
    p.push_back({R.at(0x009D66), {0x31FC, 0x00B4, 0xA346}, {0x31FC, (uint16_t)((430 + 2 * E) / 3 + 2), 0xA346}});
    return p;
}

bool wide_gameplay(const MegaDrive& md) {
    // mode 5, or 7: the attract demo (a level played from recorded input)
    const uint16_t mode = md.ram16(MODE_W) & 0x7FFF;
    return (mode == 5 || mode == 7) && md.ram16(0xFFFF1A) == 1;
}
// The frozen GAME OVER screen: the level and status bar stay on screen.
bool game_over(const MegaDrive& md) {
    return (md.ram16(MODE_W) & 0x7FFF) == 0x0F && md.ram16(0xFFFF1A) == 2;
}

// Status bar (window plane, lines 192..223): Power stays left, Points goes right, the middle
// boxes stay centred; gaps repeat plain pixel columns 78 (left) and 237 (right).
std::vector<int16_t> hud_map(int E, bool anchor) {
    std::vector<int16_t> m(320 + 2 * E);
    for (int x = 0; x < (int)m.size(); x++) {
        if (!anchor) m[x] = (int16_t)(x >= E && x < E + 320 ? x - E : -1);
        else if (x < 79) m[x] = (int16_t)x;
        else if (x < 79 + E) m[x] = 78;
        else if (x < 239 + E) m[x] = (int16_t)(x - E);
        else if (x < 239 + 2 * E) m[x] = 237;
        else m[x] = (int16_t)(x - 2 * E);
    }
    return m;
}

uint8_t bus8(const MegaDrive& md, uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) return md.ram8(a);
    return a < md.rom.size() ? md.rom[a] : 0;
}

// The patched streamer keeps map columns [X-96, X+416) of each 512-px plane (docs/re_qs_wide.md §2):
// enough up to E = 80. A wider view needs more on both sides, so cells outside that window are drawn
// here straight from the level map (chunk table -> 16x16 metatiles per 256-px chunk -> 2x2 name-table
// words, the game's own lookup), with the H/V scroll the VDP used for this line. Line-scrolled
// background bands rely on their map rows repeating every 512 px, so they come out the same.
void draw_far_columns(const MegaDrive& md, int line, uint8_t* buf, bool plane_b) {
    const VDP& v = md.vdp;
    const uint32_t L = plane_b ? 0xFF8540 : 0xFF8500;
    const int E = v.ext, TW = 320 + 2 * E;
    const int camx = (int16_t)md.ram16(L + 0x24), camy = (int16_t)md.ram16(L + 0x20);
    const int width = (int16_t)md.ram16(L + 0x00) + 320, height = (int16_t)md.ram16(L + 0x08) + 224;
    const uint32_t chunks = md.ram32(L + 0x10), blocks = md.ram32(L + 0x14), mtiles = md.ram32(L + 0x18);
    const uint32_t hsb = (v.reg[13] & 0x3F) << 10;
    int hoff;
    switch (v.reg[11] & 3) {
    case 0: hoff = 0; break;
    case 1: hoff = (line & 7) * 4; break;
    case 2: hoff = (line & ~7) * 4; break;
    default: hoff = line * 4; break;
    }
    const int px0 = -(v.vram16(hsb + hoff + (plane_b ? 2 : 0)) & 0x3FF) & 0x1FF;
    const int wx0 = camx + (((px0 - camx) + 256) & 0x1FF) - 256;        // world x of view x 0
    const int py = (line + (v.vsram[plane_b ? 1 : 0] & 0x3FF)) & 0xFF;  // 256-px tall plane
    const int wy = camy + line + (((py - (camy + line)) + 128) & 0xFF) - 128;
    if (wy < 0 || wy >= height) return;
    // Judge what the plane holds by the scroll shown on this line (VBlank wrote it together with the
    // queued columns); X in RAM may already be the next frame's.
    const int first = std::max(wx0 - 96, 0) >> 4;
    const int lo = (first + 1) * 16, hi = (first + 31) * 16;            // columns the plane surely holds
    uint8_t row[8];
    for (int x = 0; x < TW;) {
        if (x >= E && x < E + 320) { x = E + 320; continue; }   // the game always keeps its own 320-px view right
        const int wx = wx0 + x - E;
        if (wx >= lo && wx < hi) { x += hi - wx; continue; }
        const int n = std::min(8 - (wx & 7), TW - x);
        if (wx >= 0 && wx < width) {
            const int idx = (int16_t)(((wy >> 8) << 5) + (wx >> 8));
            const uint8_t chunk = bus8(md, chunks + idx);
            const int d5 = (int16_t)((chunk << 8) + ((wx >> 4) & 15) + (wy & 0xF0));
            const uint32_t off = bus8(md, blocks + d5) * 8u + ((wy & 8) ? 4 : 0) + ((wx & 8) ? 2 : 0);
            v.pattern_row((uint16_t)(bus8(md, mtiles + off) << 8 | bus8(md, mtiles + off + 1)), wy & 7, row);
            memcpy(buf + x, row + (wx & 7), n);
        }
        x += n;
    }
}

// Screens that are a 320-px picture with sprites crossing it: the world map (mode 2), and the attract intro after its
// cast scroll - Donald with the map, the map picture sliding in, the title (mode 1 step 3; module_state[4] counts the
// frames since the scroll ended, see post_frame). They are shown wide so that the sprites cross the whole view (the
// map's airplane, the title's Donald and plane); the planes hold leftovers beside the picture, which line_hook blanks.
bool map_screen(const MegaDrive& md) { return (md.ram16(MODE_W) & 0x7FFF) == 0x02; }
bool title_screen(const MegaDrive& md) {
    return (md.ram16(MODE_W) & 0x7FFF) == 0x01 && md.ram16(0xFFFF1A) == 3 && md.ram16(0xFFFC04) != 0 && md.module_state[4] > 2;
}

// The pause screen (mode 8): the inventory panels on a green chequered background, the status bar below. Shown wide:
// the status bar is laid out as in play (hud_map), and the background, whose pattern repeats every 16 px and fills
// the picture's outer 16 px on both sides, is repeated outwards over the level's leftovers.
bool pause_screen(const MegaDrive& md) { return (md.ram16(MODE_W) & 0x7FFF) == 0x08; }
// The START / OPTIONS menu and the options screen (mode $0B): panels on the same green chequered background, which the
// menu keeps in plane B (the pause screen keeps it in plane A), 320 px wide with nothing beyond it.
bool menu_screen(const MegaDrive& md) { return (md.ram16(MODE_W) & 0x7FFF) == 0x0B; }
// Both screens' background is a 16 x 16 px pattern and the picture's leftmost 16 px are always plain background (the
// panels start further in; the right edge can hold a dialog box or a panel's border). So the margins on both sides
// repeat those 16 px with the pattern's own phase: x - E (the picture's x) modulo 16 - the same for the left margin
// (negative x) and the right one (x >= 320, a multiple of 16).
void repeat_background(const MegaDrive& md, uint8_t* plane_a, uint8_t* plane_b) {
    const int E = md.vdp.ext;
    for (uint8_t* p : {plane_a, plane_b}) {
        for (int i = 0; i < E; i++) p[i] = p[E + ((i - E) & 15)];
        for (int i = E + 320; i < 320 + 2 * E; i++) p[i] = p[E + ((i - E) & 15)];
    }
}

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    MegaDrive& md = *(MegaDrive*)host;
    if (menu_screen(md) || (pause_screen(md) && !(md.vdp.reg[18] == 0x98 && line >= 192))) {
        repeat_background(md, plane_a, plane_b);
        return;
    }
    if (map_screen(md) || title_screen(md)) {
        const int E = md.vdp.ext, TW = 320 + 2 * E;
        memset(plane_a, 0, E);
        memset(plane_a + E + 320, 0, E);
        // The title's map picture (plane B, at plane x 320) slides in from the right: full-screen H scroll 0 -> -320,
        // 16 px a frame, from the 4:3 edge. Wide, the line is moved right in proportion, so the picture comes in
        // from the view's edge instead, and arrives at the same moment.
        int left = E;   // where the picture's left edge is on this line
        if (title_screen(md)) {
            const int hmode = md.vdp.reg[11] & 3, hoff = hmode == 0 ? 0 : hmode == 3 ? line * 4 : (line & ~7) * 4;
            const int sc = (int16_t)(md.vdp.vram16(((md.vdp.reg[13] & 0x3F) << 10) + hoff + 2) << 6) >> 6;   // 10 bits, signed
            if (sc > -320 && sc <= 0) {
                const int d = (320 + sc) * E / 320;
                memmove(plane_b + d, plane_b, TW - d);
                left = E + 320 + sc + d;
            }
        }
        memset(plane_b, 0, std::min(left, TW));
        if (left + 320 < TW) memset(plane_b + left + 320, 0, TW - left - 320);
        return;
    }
    if (!(wide_gameplay(md) || game_over(md) || pause_screen(md))) return;
    const bool hud = md.vdp.reg[18] == 0x98 && line >= 192;
    if (md.vdp.ext > 80 && !hud) {
        draw_far_columns(md, line, plane_a, false);
        draw_far_columns(md, line, plane_b, true);
        return;
    }
    if (!hud) return;
    static std::vector<int16_t> map[2][128];
    bool anchor = md.vdp.hook_flags & 1;
    std::vector<int16_t>& m = map[anchor][md.vdp.ext & 127];
    if (m.empty()) m = hud_map(md.vdp.ext, anchor);
    hud_remap_window_line(md.vdp, line, plane_a, plane_b, m);
}

class QuackShot : public GameModule {
public:
    const char* id() const override { return "qs"; }
    const char* title() const override { return "QuackShot"; }
    const char* subtitle() const override { return "Starring Donald Duck"; }
    const char* year() const override { return "1991"; }
    uint32_t rom_crc32() const override { return 0x88C8DD94; }
    uint32_t rom_size() const override { return 0x80000; }
    const char* rom_hint() const override { return "the QuackShot (World REV 00 or REV 01) ROM"; }
    // Both releases play with the same widescreen, cheats, level select and achievements (see "releases" above).
    std::vector<RomVariant> rom_variants() const override {
        return {{0x88C8DD94, 0x80000, "REV 00", "the QuackShot (World REV 00) ROM"},
                {0x1801098B, 0x140000, "REV 01", "the QuackShot (World REV 01) ROM"}};
    }
    // An MD+ soundtrack patch made for REV 00 carried over to REV 01. Such a patch hooks the game's sound request (at
    // $7FED8, in the sound code at the end of the ROM) and adds its own code after the end of the ROM ($80000 in REV 00,
    // $140000 in REV 01: $C0000 higher). REV 01 has the sound code higher by about that much, byte for byte: the sound
    // data and the code at the very end $C0000 up, the ending bank and the start of the free space after it $C0006 (the
    // SFX bank before them is 6 bytes longer); the data between them has moved by other amounts. So a change inside the
    // ROM is carried over only where a known shift applies and only over bytes REV 01 has the same, the added code moves
    // as it is, and the absolute addresses of its jumps into that part of the ROM move with it. Anything else is
    // refused, not guessed at (the caller also checks the result's behaviour).
    bool can_port_patch(int from, int to) const override { return from == 0 && to == 1; }
    static int tail_shift(uint32_t a) {   // REV 01 address minus REV 00 address, -1: no known shift here
        if (a >= 0x77000 && a < 0x7A0C4) return 0xC0000;
        if (a >= 0x7A0C4 && a < 0x7AFFB) return 0xC0006;
        if (a >= 0x7FD44 && a < 0x100000) return 0xC0000;   // (the last $2BC bytes of the ROM and what a patch adds after it)
        return -1;
    }
    bool port_patch(const std::vector<PatchEdit>& edits, uint32_t from_size, const std::vector<uint8_t>* orig,
                    std::vector<uint8_t>& target) const override {
        if (from_size != 0x80000 || target.size() != 0x140000 || (orig && orig->size() != from_size)) return false;
        std::vector<int16_t> ov(0x100000, -1);   // what the patch writes, by REV 00 address (-1: leaves alone)
        for (const PatchEdit& e : edits) {
            if ((uint64_t)e.offset + e.bytes.size() > ov.size()) return false;
            for (size_t k = 0; k < e.bytes.size(); k++) ov[e.offset + k] = e.bytes[k];
        }
        ov[0x18E] = ov[0x18F] = -1;                // (the checksum is recalculated)
        // jsr / jmp / lea (xxx).l into the part of the ROM that moves: the address moves with it
        auto relocate = [](std::vector<uint8_t>& bytes) {
            for (size_t k = 0; k + 6 <= bytes.size(); k += 2) {
                const uint16_t op = (uint16_t)((bytes[k] << 8) | bytes[k + 1]);
                if (!(op == 0x4EB9 || op == 0x4EF9 || (op & 0xF1FF) == 0x41F9)) continue;
                uint32_t addr = ((uint32_t)bytes[k + 2] << 24) | (bytes[k + 3] << 16) | (bytes[k + 4] << 8) | bytes[k + 5];
                if (addr < 0x70000 || addr >= 0x100000) continue;
                const int as = tail_shift(addr);
                if (as < 0) return false;
                addr += (uint32_t)as;
                bytes[k + 2] = (uint8_t)(addr >> 24); bytes[k + 3] = (uint8_t)(addr >> 16);
                bytes[k + 4] = (uint8_t)(addr >> 8);  bytes[k + 5] = (uint8_t)addr;
                k += 4;
            }
            return true;
        };
        std::vector<uint8_t> out = target;
        size_t i = 0;
        while (i < from_size) {   // changes inside the ROM
            if (ov[i] < 0) { i++; continue; }
            size_t j = i + 1, quiet = 0;       // a change, with the gaps of up to 8 bytes the patch leaves inside it
            while (j < from_size && quiet < 8) {
                if (ov[j] >= 0) quiet = 0; else quiet++;
                j++;
            }
            j -= quiet;
            // (from the word before the change: it may begin an instruction whose address the change is the end of)
            const size_t s = i >= 2 ? (i & ~(size_t)1) - 2 : 0;
            const int sh = tail_shift((uint32_t)i);
            if (sh < 0 || tail_shift((uint32_t)s) != sh || tail_shift((uint32_t)(j - 1)) != sh) return false;
            // Without REV 00's ROM to check against, only the last part of the sound code, which REV 01 has byte for byte
            // where the patch works (the sound request), is changed.
            if (!orig && (i < 0x7FD44 || sh != 0xC0000)) return false;
            // the place in REV 01 must hold what REV 00 had there (the same code, or free space)
            if (orig && memcmp(&target[s + sh], &(*orig)[s], j - s) != 0) return false;
            std::vector<uint8_t> bytes(target.begin() + s + sh, target.begin() + j + sh);
            for (size_t k = s; k < j; k++) if (ov[k] >= 0) bytes[k - s] = (uint8_t)ov[k];
            if (!relocate(bytes)) return false;
            memcpy(&out[s + sh], bytes.data(), bytes.size());
            i = j;
        }
        size_t end = ov.size();            // what the patch adds after the end of the ROM (padding of $FF is not)
        while (end > from_size && (ov[end - 1] < 0 || ov[end - 1] == 0xFF)) end--;
        if (end > from_size) {
            std::vector<uint8_t> bytes(end - from_size, 0xFF);
            for (size_t k = from_size; k < end; k++) if (ov[k] >= 0) bytes[k - from_size] = (uint8_t)ov[k];
            if (!relocate(bytes)) return false;
            out.resize(end + 0xC0000, 0xFF);
            memcpy(&out[from_size + 0xC0000], bytes.data(), bytes.size());
        }
        target = std::move(out);
        return true;
    }
    // REV 00 keeps the id its save states always had; REV 01's are separate (they hold that release's code state).
    std::string state_id(const MegaDrive& md) const override { return rev_of(md).sites ? "qs_r01" : "qs"; }
    const char* rom_names() const override { return "QUACKSHOT|ILOVEDONALDDUCK"; }
    uint32_t accent_color() const override { return 0xFFFFA23A; }
    int max_ext() const override { return 108; }  // up to 2.39:1 (536 px); beyond 80 the far columns are drawn from the map

    static std::vector<RomPatch> patch_list(const Rev& R, const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("inf_lives")) add(inf_lives_patches(R));
        if (cfg.cheat("inf_health")) add(inf_health_patches(R));
        if (cfg.cheat("invincible")) add(invincible_patches(R));
        if (cfg.cheat("inf_ammo")) add(inf_ammo_patches(R));
        if (cfg.cheat("endless_quack")) add(endless_quack_patches(R));
        add(stream_patches(R));
        if (cfg.ext > 0) add(wide_patches(R, cfg.ext, cfg.cheat("spawn_fix")));
        return p;
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override {
        return patches_safe(md, patch_list(rev_of(md), cfg));
    }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        const Rev& R = rev_of(md);
        apply_patches(md, patch_list(R, cfg));
        // From 16:9 or narrower to 2:1 / 21:9 mid-level (a setting, or a narrower save state loaded) the game has
        // created the spawn columns up to (X>>7)+3 only, while the far trigger goes on from +5: until the camera
        // reaches another 128-px cell, the original-column pass creates every entry of its column, not only the
        // deferred ones. module_state[0]: bit 31 + the camera cell at the switch; [1]: width last configured + 1.
        // (bit 16: the far pass was on. States from builds without it hold the width alone, and the far pass then began
        // above 16:9, so a 16:9 one counts as narrow.)
        const uint32_t prev = md.module_state[1];
        const bool prev_far = (prev & 0x10000) != 0 || (int)(prev & 0xFFFF) - 1 > 40;
        if (cfg.ext < kFarFrom) md.module_state[0] = 0;
        else if (prev && !prev_far) md.module_state[0] = 0x80000000u | (md.ram16(0xFF8524) >> 7);
        md.module_state[1] = ((uint32_t)cfg.ext + 1) | (cfg.ext >= kFarFrom ? 0x10000u : 0u);
        if (md.module_state[0]) md.patch_rom16(R.spawn_all_flag(), 0x7C01);
        md.vdp.line_hook = cfg.ext > 0 ? line_hook : nullptr;
        md.vdp.sprite_x10 = cfg.ext > 64;   // screen x up to 427 needs VDP X beyond 511
        md.vdp.hook_host = &md;
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
    }

    // module_state[7]: bit0 free movement, bit1 permanent quack attack, bit2 all weapons were on last
    // frame, bit3 the game handed out the red plunger while All Weapons was on; module_state[6]: the
    // weapon flags when "All Weapons" was switched on (or when a new game / level select started with
    // it on - module_state is cleared on power-on and kept in save states).
    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        // module_state[3]: plane A X this frame is drawn with - read now, as VBlank at the end of the frame
        // already prepares the next one. With full-screen H scroll that is the uploaded scroll value;
        // otherwise the camera copy VBlank used ($8FB4) minus the screen shake / ship sway ($8F86, which
        // is also added to every sprite).
        const int cam = (int16_t)md.ram16(0xFF8FB4);
        int view_x = cam - (int16_t)md.ram16(0xFF8F86);
        if ((md.vdp.reg[11] & 3) == 0) {
            const int px0 = -(md.vdp.vram16((md.vdp.reg[13] & 0x3F) << 10) & 0x3FF) & 0x1FF;
            view_x = cam + (((px0 - cam) + 256) & 0x1FF) - 256;
        }
        if (!pause_screen(md)) md.module_state[3] = (uint16_t)view_x;   // (paused: the scroll table is the pause screen's)
        uint32_t& was = md.module_state[7];
        const bool free_move = cfg.cheat("free_move"), always_quack = cfg.cheat("always_quack"),
                   all_weapons = cfg.cheat("all_weapons");
        uint8_t mode = md.ram8(MODE);
        bool playing = mode == M_GAMEPLAY || mode == M_PAUSE;

        // Cheats that change game state must also undo it when switched off.
        if (!free_move && (was & 1)) {
            if (md.ram8(FREE_MOVE)) exit_free_move(md, mode == M_GAMEPLAY);
            was &= ~1u;
        }
        if (!always_quack && (was & 2)) {
            if (md.ram8(QUACK)) md.set_ram16(QUACK_TIME, 1);   // ends through the game's own path next frame
            was &= ~2u;
        }
        if (all_weapons && (was & 4) && md.ram8(PLUNGER) == 1) was |= 8;   // Goofy's gift (the cheat keeps it at 2)
        if (!all_weapons && (was & 4)) {
            restore_weapons(md, (was & 8) != 0);
            was &= ~12u;
        }
        if (all_weapons && !(was & 4)) {
            md.module_state[6] = md.ram8(BUBBLEGUM) | (md.ram8(NOTE_RED_PLUNGER) << 8) | (md.ram8(GREEN_PLUNGER) << 16);
            was |= 4;
        }
        if (free_move) was |= 1;
        if (always_quack) was |= 2;

        if (!playing) return;
        if (cfg.cheat("inf_ammo") && (md.ram16(POPCORN) < 95 || md.ram16(GUM) < 50)) {
            md.set_ram16(POPCORN, 95);
            md.set_ram16(GUM, 50);
            md.set_ram8(AMMO_DIRTY, md.ram8(AMMO_DIRTY) | 1);
        }
        if (always_quack && mode == M_GAMEPLAY) {
            md.set_ram8(QUACK, 0xFF);
            md.set_ram16(QUACK_TIME, 360);
            md.set_ram16(TEMPER, 5);
        }
        if (all_weapons && (md.ram8(BUBBLEGUM) != 0xFF || md.ram8(PLUNGER) != 2)) {
            md.set_ram8(0xFF0000 | BUBBLEGUM, 0xFF);
            md.set_ram8(0xFF0000 | NOTE_RED_PLUNGER, 0xFF);
            md.set_ram8(0xFF0000 | GREEN_PLUNGER, 0xFF);
            set_plunger(md, 2);
        }
        if (free_move) md.set_ram8(FREE_MOVE, 0xFF);
    }

    void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        const Rev& R = rev_of(md);
        // The narrow -> wide switch flag (configure) goes off once the camera has reached another 128-px cell -
        // judged by its previous-frame X ($852C): the spawner sees a crossing a frame after $8524 shows it.
        if (md.module_state[0] && (uint32_t)(md.ram16(0xFF852C) >> 7) != (md.module_state[0] & 0xFFFF)) {
            md.module_state[0] = 0;
            if (E >= kFarFrom) md.patch_rom16(R.spawn_all_flag(), 0x7C00);
        }
        // The first crossing after a turn (wide_patches, TRIGGER): $852C -> $8524 is the step the spawner looks at
        // next frame. module_state[2]: direction of the last crossing (1 right, 0xFF left, 0 none yet).
        if (E > 0) {
            const int from = md.ram16(0xFF852C) >> 7, to = md.ram16(0xFF8524) >> 7;
            if (from != to) {
                const int dir = to > from ? 1 : -1, last = (int8_t)(uint8_t)md.module_state[2];
                md.module_state[2] = (uint8_t)dir;
                md.patch_rom16(R.left_flag(), dir < 0 && last > 0 ? LEFT_ON : LEFT_OFF);
                if (E >= kFarFrom) md.patch_rom16(R.spawn_all_flag(), (dir > 0 && last < 0) || md.module_state[0] ? 0x7C01 : 0x7C00);
            }
        }
        // The "spawns behind the player" option: an enemy moved out of view at its creation catches up with where it would
        // be at 4:3. It eases in gently: the extra speed is left/64 + 1/8 px a frame, at most 1.5 - it is still a little
        // fast when it passes the place 4:3 made it at, and settles to its own pace over the next seconds (the birds
        // have less time, see below). The object
        // field $3E holds the pixels left (low 11 bits; bit 11: to the left) and the sixteenths of a pixel carried
        // over (high 4); anything above the most the option moves is not ours.
        // The sprite cycle keeps step with the ground covered. All objects animate through the same fields ($07 frame,
        // $08 tick, counted up to the period at $09): the tick gets the frames' worth of the extra pixels at the
        // enemy's own pace (birds 1.5 px a frame, the rest 1), so a faster walk is a faster stride, not a slide.
        // The snake charmers $52 do not walk: they stand for a 60-frame cycle and step 11 px when it wraps. For them
        // the cycle runs faster (up to 4 times, easing off to 2) and the distance is added to their steps only (up to
        // 15 px a step, easing off to 3): longer, quicker steps, standing still in between as they do. Left alone they
        // shuffle 11 px back and forth until Donald is near, so they do not make up the distance by themselves.
        if (E > 0 && cfg.cheat("spawn_fix") && wide_gameplay(md)) {
            const uint16_t most = (uint16_t)(160 + E + kBehindMargin);
            for (uint32_t s = 0xFF9000; s < 0xFF9C00; s += 0x40) {
                const uint16_t t = md.ram16(s), v = md.ram16(s + kCatchUpField), rem = v & 0x07FF, left = v & 0x0800;
                if (!rem || rem > most || std::find(std::begin(kBehindTypes), std::end(kBehindTypes), t) == std::end(kBehindTypes))
                    continue;
                const uint32_t period = md.ram8(s + 0x09), tick = md.ram8(s + 0x08);
                auto hurry = [&](unsigned ticks) {   // the sprite cycle, that many frames further (never past its wrap)
                    if (period > 1 && ticks) md.set_ram8(s + 0x08, (uint8_t)std::min<unsigned>(tick + ticks, period - 1));
                };
                unsigned acc = v >> 12, n = 0;
                if (t == 0x52) {
                    // acc: 1 = this cycle's step has been lengthened
                    const bool first = md.ram8(s + 0x07) == 0;
                    if (!first) acc &= ~1u;
                    const bool step_now = md.ram16(s + 0x02) != 0 && first && tick <= 1 && !(acc & 1);
                    if (v & 0x8000) {   // caught up: kCharmerExtra more of its own steps clear of the terrain
                        unsigned k = rem;
                        if (step_now) { k--; acc |= 1; }
                        md.set_ram16(s + kCatchUpField, k ? (uint16_t)(0x8000 | ((acc & 1) << 12) | k) : 0);
                        continue;
                    }
                    if (step_now) {   // it has just stepped
                        // (its own 11 px, to the right while it catches up - see CHARM_AT - count as well, so
                        // that it is done where 4:3 made it, past the blocks, and goes its own way from there)
                        const unsigned own = std::min<unsigned>(rem, 11);
                        n = std::min<unsigned>(rem - own, std::max(3u, std::min(15u, rem / 5u)));
                        md.set_ram16(s + 0x10, (uint16_t)(md.ram16(s + 0x10) + n));
                        md.set_ram16(s + kCatchUpField, rem == n + own ? (uint16_t)(0x9000 | kCharmerExtra)
                                                                       : (uint16_t)((1u << 12) | (rem - n - own)));
                        continue;
                    } else {
                        hurry(std::max(1u, std::min(3u, rem / 24u)));
                    }
                } else {
                    // The swoop-in birds (and the penguin) fly a fixed script and attack at its end only if Donald is
                    // within 24 px: they have about 80 frames to be on their 4:3 track, so they keep the quicker curve
                    // (left/40 + 1/4 px, at most 2).
                    const bool bird = t == 0x0D || t == 0x7C, timed = bird || t == 0x4B;
                    acc += timed ? std::min(32u, rem * 2u / 5u + 4u) : std::min(24u, rem / 4u + 2u);   // 1/16 px
                    n = std::min<unsigned>(rem, acc >> 4);
                    acc &= 15;
                    if (t != 0xB0 || md.ram16(s + 0x02) == 2) hurry(bird ? (2 * n + 1) / 3 : n);   // (a statue: while it walks)
                }
                if (n) md.set_ram16(s + 0x10, (uint16_t)(md.ram16(s + 0x10) + (left ? -(int)n : (int)n)));
                md.set_ram16(s + kCatchUpField, rem == n ? 0 : (uint16_t)((acc << 12) | left | (rem - n)));
            }
        }
        const bool level = wide_gameplay(md) || game_over(md) || pause_screen(md);
        // A switch into play lands mid-frame (the HUD hook still sees the old mode on the status bar
        // lines), so the first frame stays 4:3. module_state[5]: level shown last frame.
        const bool settled = level && (md.module_state[5] & 1);
        md.module_state[5] = (md.module_state[5] & ~1u) | (level ? 1 : 0);
        // Wide: gameplay, the frozen game-over screen and the cast scroll of the attract intro
        // (mode 1 while $FFFC04 = 0; the map and title picture after it are 320 px, beside leftovers).
        // $FFFC04 changes 2 frames before the cave picture is cut, so the scroll keeps its width for those.
        // module_state[4]: frames since $FFFC04 left 0 in mode 1.
        bool cast = false;
        if ((md.ram16(MODE_W) & 0x7FFF) == 0x01) {
            if (md.ram16(0xFFFC04) == 0) md.module_state[4] = 0;
            else if (md.module_state[4] < 255) md.module_state[4]++;
            cast = md.module_state[4] <= 2;
        }
        info.gameplay = settled || cast || (E > 0 && (map_screen(md) || title_screen(md) || menu_screen(md)));
        if (!settled || E == 0) return;
        int x = (int16_t)(uint16_t)md.module_state[3], xmin = md.ram16(0xFF8504), xmax = md.ram16(0xFF8500);
        if (!pause_screen(md)) {   // (the pause screen fills the view whatever the camera's place in the level)
            info.mask_left = std::clamp(E - (x - xmin), 0, E);
            info.mask_right = std::clamp(x + E - xmax, 0, E);
        }
        if (md.vdp.reg[18] == 0x98) {
            info.mask_lines = 192;
            info.pillar_below = !cfg.hud_anchor;
        }
    }

    std::vector<AchievementDef> achievements() const override {
        return {
            {"qs_herokey", "Key to Adventure", "Get the Hero Key.", 0},
            {"qs_gyro", "Gyro's Gift", "Get Gyro's bubble gum.", 0},
            {"qs_dracula", "Stake Out", "Defeat Dracula Duck.", 0},
            {"qs_tiger", "Tiger Tamer", "Beat the Maharajah's tiger.", 0},
            {"qs_ghosts", "Ghost Ship Gone", "Beat the Viking ghosts.", 0},
            {"qs_pete", "Pete's Comeuppance", "Beat Pete.", 0},
            {"qs_treasure", "The Great Duck Treasure", "Finish the game.", 0},
            {"qs_steel", "Duck of Steel", "Clear a whole location without losing a heart.", 0},
            {"qs_untouchable", "Untouchable Duck", "Finish the game from a new game without losing a life.", 0},
            {"qs_minecart", "Mine Cart Maverick", "Ride Egypt's mine cart without a hit.", 0},
            {"qs_quack", "Hot Under the Collar", "Eat 5 chilli peppers and go into Quack Attack.", 0},
            {"qs_rampage", "Rampage", "Defeat 5 enemies in one Quack Attack.", 5},
            {"qs_climber", "Wall Crawler", "Climb a wall using your plungers as steps.", 0},
            {"qs_bird", "Hitching a Ride", "Hang on to a flying bird with the green plunger.", 0},
            {"qs_gum", "Gum Shoe", "Blow up Transylvania's wall with bubble gum.", 0},
            {"qs_popcorn", "Popcorn Hoarder", "Fill your popcorn to the maximum.", 95},
            {"qs_flyer", "Frequent Flyer", "Take 15 plane trips.", 15},
            {"qs_score", "Seven-Figure Duck", "Score 1,000,000 points.", 0},
        };
    }

    // ---- Achievements: the rules and their verification on real gameplay are in tools/qa/ach/qs (findings.md,
    // qs_rules.py, v_*.py). Indices follow achievements().
    enum { A_HEROKEY, A_GYRO, A_DRACULA, A_TIGER, A_GHOSTS, A_PETE, A_TREASURE, A_STEEL, A_UNTOUCHABLE, A_MINECART,
           A_QUACK, A_RAMPAGE, A_CLIMBER, A_BIRD, A_GUM, A_POPCORN, A_FLYER, A_SCORE };
    enum { S_FIRED, S_STEEL_LOC, S_STEEL_CLEAN, S_CART, S_QKILLS, S_PLUNGER_Y, S_HAVE_PLUNGER_Y, S_ON_PLUNGER };
    // run bits: the playthrough began with New Game (seen as mode $0C -> $0E); a life was lost since
    enum : uint32_t { R_NEWGAME = 1, R_LIFE_LOST = 2 };

    void achievement_frame(const MegaDrive& md, AchievementCtx& c) const override {
        if (c.prev.size() != 0x10000) return;
        const RamView s{md.ram}, p{c.prev.data()};
        auto fire = [&](int i) {
            if (c.scratch[S_FIRED] & (1 << i)) return;
            c.scratch[S_FIRED] |= 1 << i;
            c.unlocked.push_back(i);
        };
        constexpr uint32_t MODE = 0xFFFF19, DEMO = 0xFFEE50, LOC = 0xFF8F34, ROOM = 0xFF8F36, ENTRY = 0xFF8F74,
                           MAP_STATE = 0xFFA344, POWER = 0xFF8F96, LIVES = 0xFF8F98, POPCORN = 0xFF8F90, QUACK = 0xFFEE62,
                           SCORE = 0xFFEF34, PENDING = 0xFF8F7A, PLAYER = 0xFFA2C0, PY = 0xFFA2D4, AIRFLAGS = 0xFFEE03,
                           ON_PLUNGER = 0xFFEE43, GUM_WALL = 0xFFEEF1;
        enum { M_MAP = 2, M_MAP2LVL = 3, M_LOAD = 4, M_GAME = 5, M_PAUSE = 8, M_ENDING = 9, M_INTRO = 0x0C,
               M_NEWGAME = 0x0E, M_GAMEOVER = 0x0F, M_CONTINUE = 0x10 };
        // real gameplay: mode 5 (the attract demo runs in mode 7 with $EE50 set; pause is mode 8)
        auto in_gameplay = [&](const RamView& r) { return r.b(MODE) == M_GAME && !r.b(DEMO); };
        const int m = s.b(MODE), pm = p.b(MODE);
        const bool play = in_gameplay(s) && in_gameplay(p);

        // New Game (title -> menu -> intro -> init) / continue (game over -> CONTINUE -> init)
        if (m == M_NEWGAME && pm != M_NEWGAME) {
            if (pm == M_INTRO) { c.run_bits = R_NEWGAME; c.scratch[S_STEEL_LOC] = 0; }
            else if (pm == M_CONTINUE) c.run_bits |= R_LIFE_LOST;
        }
        // story flags 0 -> $FF in a frame that started in gameplay (a continue restores them in mode $0E; the
        // level select writes them on the map)
        const bool flag_frame = (pm == M_GAME || pm == M_PAUSE) && !p.b(DEMO);
        static const uint32_t kStory[6] = {0xFFEEE0, 0xFFEEE7, 0xFFEEF3, 0xFFEEEE, 0xFFEEED, 0xFFEEF8};
        if (flag_frame) {
            for (int i = 0; i < 6; i++)
                if (!p.b(kStory[i]) && s.b(kStory[i])) fire(A_HEROKEY + i);
            if (!p.b(GUM_WALL) && s.b(GUM_WALL) && s.w(LOC) == 3) fire(A_GUM);   // (every breakable block sets it)
        }
        // the figurine switches gameplay to the ending (mode 9)
        if (m == M_ENDING && pm == M_GAME && !p.b(DEMO)) {
            fire(A_TREASURE);
            if (c.run_counts && (c.run_bits & R_NEWGAME) && !(c.run_bits & R_LIFE_LOST)) fire(A_UNTOUCHABLE);
        }
        if ((c.run_bits & R_NEWGAME) && (s.w(LIVES) < p.w(LIVES) || m == M_GAMEOVER)) c.run_bits |= R_LIFE_LOST;

        if (play && s.b(QUACK) && !p.b(QUACK)) fire(A_QUACK);

        // Rampage: enemies defeated during one Quack Attack. A defeat: same type as last frame (not $12, which only
        // stuns again), hit by a killing attack (+$0F = $FF), hit points +$39 down to 0 or below, a new state.
        int kills = 0;
        if (play) {
            auto check = [&](uint32_t a) {
                const int t = s.w(a);
                if (!t || t != p.w(a) || t == 0x12) return;
                const int hp0 = p.b(a + 0x39), hp1 = s.b(a + 0x39);
                if (s.b(a + 0x0F) == 0xFF && hp1 != hp0 && ((hp0 - hp1) & 0xFF) < 0x10 && (hp1 == 0 || hp1 >= 0x80) &&
                    s.w(a + 2) != p.w(a + 2))
                    kills++;
            };
            for (uint32_t i = 0; i < 48; i++) check(0xFF9000 + i * 0x40);
            for (uint32_t i = 0; i < 16; i++) check(0xFF9EC0 + i * 0x40);
        }
        if (s.b(QUACK) || p.b(QUACK)) {
            if (play && kills) {
                c.scratch[S_QKILLS] += kills;
                c.progress.push_back({A_RAMPAGE, std::min(c.scratch[S_QKILLS], 5)});
                if (c.scratch[S_QKILLS] >= 5) fire(A_RAMPAGE);
            }
        }
        if (!s.b(QUACK)) c.scratch[S_QKILLS] = 0;

        if (play && s.w(POPCORN) >= 95 && p.w(POPCORN) < 95) fire(A_POPCORN);
        if (play && s.w(POPCORN) > p.w(POPCORN)) c.progress.push_back({A_POPCORN, std::min<int>(s.w(POPCORN), 95)});
        // score (+ the points still counting up) reaches 1,000,000 (stored /10)
        const uint32_t total = s.l(SCORE) + s.w(PENDING), ptotal = p.l(SCORE) + p.w(PENDING);
        if (play && total >= 100000 && total > ptotal) fire(A_SCORE);
        // Frequent Flyer: one count per plane departure from the world map (kept across sessions by the app)
        if (m == M_MAP && pm == M_MAP && p.w(MAP_STATE) == 1 && s.w(MAP_STATE) == 2) c.counted.push_back(A_FLYER);
        // Hitching a Ride: carried by a bird on the green plunger (player state $14/$15)
        if (play && ((s.w(PLAYER + 2) & 0x7FFE) == 0x14) && ((p.w(PLAYER + 2) & 0x7FFE) != 0x14)) fire(A_BIRD);

        // Wall Crawler: two plunger platforms in a row (a red / green plunger shot into a wall), the second higher,
        // no ground in between
        if (play) {
            bool onp = s.b(ON_PLUNGER) != 0;
            for (uint32_t i = 0; i < 11 && !onp; i++) {
                const uint32_t a = 0xFF9C00 + i * 0x40;
                if ((s.w(a) == 0x61 || s.w(a) == 0x62) && (s.b(a + 0x0E) & 0x40)) onp = true;
            }
            if (onp && !c.scratch[S_ON_PLUNGER]) {
                const int y = s.sw(PY);
                if (c.scratch[S_HAVE_PLUNGER_Y] && y < c.scratch[S_PLUNGER_Y] - 8) fire(A_CLIMBER);
                c.scratch[S_PLUNGER_Y] = y;
                c.scratch[S_HAVE_PLUNGER_Y] = 1;
            } else if (!onp && !(s.b(AIRFLAGS) & 1)) {
                c.scratch[S_HAVE_PLUNGER_Y] = 0;   // back on the ground
            }
            c.scratch[S_ON_PLUNGER] = onp;
        } else {
            c.scratch[S_ON_PLUNGER] = c.scratch[S_HAVE_PLUNGER_Y] = 0;
        }

        // Duck of Steel: one visit (arrival by plane, mode 3) that completes the location's objective for that visit
        // (its first objective flag 0 -> $FF; the ending on the Island) without losing a heart
        static const uint32_t kObjectives[10][3] = {
            {0, 0, 0}, {0xFFEEC0, 0xFFEEE0, 0xFFEEE7}, {0xFFEEC1, 0xFFEEE4, 0}, {0xFFEEF3, 0, 0}, {0xFFEEED, 0, 0},
            {0xFFEEE1, 0xFFEEF9, 0}, {0xFFEEE2, 0, 0}, {0xFFEEE3, 0, 0}, {0xFFEEF8, 0, 0}, {0, 0, 0}};
        if (m == M_MAP2LVL && pm != M_MAP2LVL) {
            const int loc = s.w(LOC);
            c.scratch[S_STEEL_LOC] = loc >= 1 && loc <= 9 ? loc : 0;
            c.scratch[S_STEEL_CLEAN] = 1;
        }
        if (const int loc = c.scratch[S_STEEL_LOC]) {
            if (m == M_MAP || m == M_GAMEOVER || m == M_CONTINUE || m == M_NEWGAME || s.w(LOC) != loc) {
                c.scratch[S_STEEL_LOC] = 0;
            } else {
                if (s.w(POWER) < p.w(POWER) || s.w(LIVES) < p.w(LIVES)) c.scratch[S_STEEL_CLEAN] = 0;
                bool done = false;
                if (flag_frame)
                    for (uint32_t a : kObjectives[loc])
                        if (a && !p.b(a) && s.b(a)) done = true;
                if (loc == 9 && m == M_ENDING && pm == M_GAME) done = true;
                if (done) {
                    if (c.scratch[S_STEEL_CLEAN]) fire(A_STEEL);
                    c.scratch[S_STEEL_LOC] = 0;
                }
            }
        }

        // Mine Cart Maverick: Egypt room 5 (entered, or reloaded after a death) to its far exit (entry 6), no hit.
        // S_CART: 0 not riding, 1 riding clean, 2 hit
        if (s.w(LOC) == 7 && s.w(ROOM) == 5 && m == M_GAME && (c.scratch[S_CART] == 0 || pm == M_LOAD)) c.scratch[S_CART] = 1;
        if (c.scratch[S_CART]) {
            if (s.w(LOC) != 7 || m == M_MAP || m == M_GAMEOVER) {
                c.scratch[S_CART] = 0;
            } else if (s.w(POWER) < p.w(POWER) || s.w(LIVES) < p.w(LIVES)) {
                c.scratch[S_CART] = 2;
            } else if (m == M_LOAD && s.w(ENTRY) == 6) {
                if (c.scratch[S_CART] == 1) fire(A_MINECART);
                c.scratch[S_CART] = 0;
            }
        }
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies can't hurt Donald. Falling off the screen still costs a life."},
            {"inf_health", "Infinite Health", "Hits knock Donald back but never remove hearts."},
            {"inf_lives", "Infinite Lives", "Losing a life never reduces the lives counter."},
            {"inf_ammo", "Infinite Ammo", "Popcorn and bubble gum never run out."},
            {"all_weapons", "All Weapons", "Bubble gum and the red and green plungers (climb walls, grab birds). Switching it off leaves the weapons Donald has earned."},
            {"endless_quack", "Endless Quack Attack", "Once Donald loses his temper, the Quack Attack never wears off."},
            {"always_quack", "Permanent Quack Attack", "Donald is always in Quack Attack mode."},
            {"free_move", "Debug Free Movement", "The developers' hidden fly mode: the D-pad moves Donald anywhere, no gravity or walls. Switch it off in open air to land (inside a wall he stays stuck, as with the game's own debug exit)."},
        };
    }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Level& l : kLevels) v.push_back({l.group, l.name});
        return v;
    }

    bool start_level(MegaDrive& md, const GameConfig& cfg, int index, int) override {
        if (index < 0 || index >= (int)(sizeof(kLevels) / sizeof(kLevels[0]))) return false;
        static const std::vector<Preset> presets = build_presets();
        const Level& lv = kLevels[index];
        md.power_on();
        configure(md, cfg);
        auto mode = [&]() { return md.ram8(MODE); };
        auto press = [&](uint16_t b, int frames, int after) {
            md.set_pad(0, b);
            for (int i = 0; i < frames; i++) md.run_frame();
            md.set_pad(0, 0);
            for (int i = 0; i < after; i++) md.run_frame();
        };
        auto frames = [&](int n) { for (int i = 0; i < n; i++) md.run_frame(); };
        // power-on -> title -> START/OPTIONS menu -> intro (skipped) -> idle world map
        if (!run_until(md, [&]() { return mode() == M_TITLE; }, 3000)) return false;
        frames(30);
        for (int i = 0; i < 60 && mode() != M_MENU; i++) press(MD_START, 2, 10);
        if (!run_until(md, [&]() { return (md.ram16(MODE_W) & 0x8000) != 0; }, 200)) return false;
        frames(60);
        press(MD_START, 2, 10);
        if (!run_until(md, [&]() { return mode() == M_INTRO; }, 300)) return false;
        for (int i = 0; i < 60 && mode() == M_INTRO; i++) press(MD_START, 2, 10);
        if (!run_until(md, [&]() { return mode() == M_MAP; }, 3000)) return false;
        if (!run_until(md, [&]() { return md.ram16(MAP_STATE) == 1; }, 600)) return false;
        frames(60);

        // story flags for this point of the adventure (working copy + the copy a continue restores)
        const Preset& p = presets[lv.preset];
        for (uint32_t a = 0xEEC0; a < 0xEF00; a++) {
            bool on = p.flags.count((uint16_t)a) != 0;
            if (lv.loc >= 1 && lv.loc <= 7 && a == cleared(lv.loc) && lv.entry == kReturnEntry[lv.loc]) on = false;
            md.set_ram8(0xFF0000 | a, on ? 0xFF : 0);
        }
        for (uint32_t i = 0; i < 0x20; i++) md.set_ram8(0xFFFCA0 + i, md.ram8(0xFFEEE0 + i));
        md.set_ram8(PLUNGER, (uint8_t)p.plunger);
        md.set_ram16(PLUNGER_PAL, PLUNGER_COLORS[p.plunger]);
        md.set_ram8(PAL_DIRTY, 1);
        md.set_ram16(GUM, (uint16_t)p.gum);
        if (p.gum) md.set_ram8(AMMO_DIRTY, md.ram8(AMMO_DIRTY) | 1);

        if (lv.loc == 0) {   // ending, via the game's own debug stage hook
            md.set_ram16(DEBUG_STAGE, 0x2F);
            press(MD_START, 2, 2);
            return run_until(md, [&]() { return mode() == M_ENDING; }, 600);
        }
        md.set_ram16(LOCATION, (uint16_t)lv.loc);
        frames(2);
        if (mode() == M_MAP && md.ram16(MAP_STATE) == 1 && lv.loc != 8 && lv.loc != 9) press(MD_START, 2, 1);
        if (!run_until(md, [&]() { return mode() == M_MAP_TO_LEVEL; }, 900)) return false;
        md.set_ram16(ENTRY, (uint16_t)lv.entry);
        md.set_ram16(LOCATION, (uint16_t)lv.loc);
        if (!run_until(md, [&]() { return mode() == M_GAMEPLAY; }, 900)) return false;
        if (lv.entry == 0) return true;
        // The game only arrives by plane at entry 0. The plane's art goes over tiles that later rooms use for
        // their backgrounds (Mexico countryside, Maharajah palace: $260+), so start other entries the way a
        // death does: mode 4 reloads the room without the plane. The plane has already asked for its engine
        // sound on this first frame; the request is still waiting in RAM, so drop it.
        auto drop_plane_sfx = [&]() { if (md.ram8(SFX_REQ) == SFX_PLANE) md.set_ram8(SFX_REQ, 0); };
        drop_plane_sfx();
        md.set_ram16(ENTRY, (uint16_t)lv.entry);
        md.set_ram16(MODE_W, 0x0004);
        if (!run_until(md, [&]() { return mode() == M_GAMEPLAY; }, 900)) return false;
        drop_plane_sfx();
        return true;
    }

    std::vector<SoundEntry> sounds() const override {
        std::vector<SoundEntry> v(std::begin(kSounds), std::end(kSounds));
        // remaining effects without a known meaning
        for (int id = 0xA0; id <= 0xE6; id++) {
            bool known = false;
            for (const SoundEntry& e : kSounds) known |= e.id == id;
            if (!known) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Effect %02X", id);
                v.push_back({id, buf, false});
            }
        }
        return v;
    }

    void jukebox_boot(MegaDrive& md) override {
        md.power_on();
        run_until(md, [&]() { return md.ram8(MODE) == M_TITLE; }, 3000);
        for (int i = 0; i < 60; i++) md.run_frame();
        freeze_68k(md);
        stop_sound(md);
        for (int i = 0; i < 4; i++) md.run_frame();   // let the driver take the stop request
    }

    void play_sound(MegaDrive& md, int id) override {
        // The ending theme lives in an alternate driver data bank (ROM $7A0C4 -> Z80 $0F00);
        // every other sound needs the normal SFX bank (ROM $792A6 -> Z80 $0DD0), which overwrites it.
        // A new music track replaces the ending, but an effect would play on top of it, so the
        // ending is stopped first. (Addresses of REV 00; REV 01 has them elsewhere and its SFX bank is 6 bytes longer.)
        const Rev& R = rev_of(md);
        if (id == 0x8C) {
            load_bank(md, R.ending_bank, 0x0F00, 0x6D8);
        } else {
            if (id >= 0xA0 && memcmp(&md.zram[0x0F00], &md.rom[R.ending_bank], 0x6D8) == 0) stop_sound(md);
            load_bank(md, R.sfx_bank, 0x0DD0, R.sfx_len);
        }
        request(md, (uint8_t)id);
    }

    void stop_sound(MegaDrive& md) override { request(md, 0xF9); }

    // The game's sound request ($7FED8) sends the ID queued in $EE53 / $EE52 to the driver; an MD+ patch hooks it.
    bool music_request(MegaDrive& md, int id) override {
        md.set_ram8(0xFFEE53, 0);
        md.set_ram8(0xFFEE52, (uint8_t)id);
        return call_68k(md, rev_of(md).sound_request);
    }


private:
    static void set_plunger(MegaDrive& md, int type) {
        md.set_ram8(PLUNGER, (uint8_t)type);
        md.set_ram16(PLUNGER_PAL, PLUNGER_COLORS[type]);
        md.set_ram8(PAL_DIRTY, 1);
    }

    // What the game's own exit from the debug fly mode does (A + START at $6006): clear the mode
    // flags, put Donald back into his normal "stand" state and reset his animation ($5EAA, d0 = 0).
    static void exit_free_move(MegaDrive& md, bool in_level) {
        for (uint32_t a : {0xFFEE07u, 0xFFEE03u, 0xFFEE44u, 0xFFEE45u, 0xFFEE41u, 0xFFEE42u}) md.set_ram8(a, 0);
        if (!in_level) return;
        const uint32_t player = 0xFFA2C0;
        md.set_ram16(player + 2, 0x8002);
        md.set_ram16(player + 4, 0);
        md.set_ram16(player + 6, 0);
        md.set_ram16(0xFF8F3C, 0x0100);
        md.set_ram16(0xFF8F3E, 0x0000);
        md.set_ram8(player + 8, 0);
    }

    // Back to the weapons Donald has earned: what he had when "All Weapons" was switched on, plus what
    // the game handed out meanwhile - the gum comes with meeting Gyro, the red plunger was seen being
    // given (Goofy), the green one comes with clearing the Viking ship. The plunger type is then derived
    // from the flags the way the game's own $5052 does.
    static void restore_weapons(MegaDrive& md, bool red_given) {
        uint32_t saved = md.module_state[6];
        uint8_t gum = (saved & 0xFF) || md.ram8(0xFF0000 | MET_GYRO) ? 0xFF : 0;
        uint8_t red = (saved & 0xFF00) || red_given ? 0xFF : 0;
        uint8_t green = (saved & 0xFF0000) || md.ram8(0xFF0000 | cleared(4)) ? 0xFF : 0;
        md.set_ram8(0xFF0000 | BUBBLEGUM, gum);
        md.set_ram8(0xFF0000 | NOTE_RED_PLUNGER, red);
        md.set_ram8(0xFF0000 | GREEN_PLUNGER, green);
        set_plunger(md, green ? 2 : red ? 1 : 0);
        if (!gum && md.ram16(0xFF8F66) == 2) {       // bubble gum selected but no longer owned
            md.set_ram16(0xFF8F66, 0);
            md.set_ram8(AMMO_DIRTY, md.ram8(AMMO_DIRTY) | 1);
        }
    }

    static void load_bank(MegaDrive& md, uint32_t rom, uint16_t z80, uint32_t len) {
        if (memcmp(&md.zram[z80], &md.rom[rom], len) != 0) memcpy(&md.zram[z80], &md.rom[rom], len);
    }
    static void request(MegaDrive& md, uint8_t id) {
        for (uint16_t slot = 0x1C0A; slot <= 0x1C0C; slot++)
            if (md.zram[slot] == 0) { md.zram[slot] = id; return; }
        md.zram[0x1C0A] = id;
    }
};

}  // namespace

GameModule* create_qs_module() { return new QuackShot(); }
