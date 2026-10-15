// QuackShot Starring Donald Duck (World, REV 00) - enhancement module.
// Addresses and techniques: docs/re_qs_game.md, docs/re_qs_wide.md.
#include "game.h"
#include "common.h"
#include "../core/md.h"
#include <cstdio>
#include <cstring>
#include <iterator>
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
constexpr uint32_t SPAWN_ALL_FLAG = 0x07B45E;   // ROM: "moveq #0/1,d6" in the spawn hook (see wide_patches)
constexpr uint32_t SFX_REQ = 0xFFEE52;    // sound effect waiting to go to the Z80 driver ($7FED8 sends it)
constexpr uint8_t SFX_PLANE = 0xB4;       // "airplane arrives", asked for by the plane object ($8024)
constexpr uint8_t M_TITLE = 0x01, M_MAP = 0x02, M_MAP_TO_LEVEL = 0x03, M_GAMEPLAY = 0x05, M_PAUSE = 0x08,
                  M_ENDING = 0x09, M_MENU = 0x0B, M_INTRO = 0x0C;
constexpr uint16_t PLUNGER_COLORS[3] = {0x02CE, 0x022E, 0x02A2};   // yellow / red / green

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
const std::vector<RomPatch> kInfLives = {{0x00481E, {0x5378, 0x8F98}, {0x4A78, 0x8F98}}};
const std::vector<RomPatch> kInfHealth = {{0x005E16, {0x9F78, 0x8F96, 0x5378, 0x8F96}, {0x4E71, 0x4E71, 0x4E71, 0x4E71}}};
const std::vector<RomPatch> kInvincible = {
    {0x00742C, {0x50F8, 0xEE09}, {0x4A38, 0xEE09}}, {0x00B866, {0x50F8, 0xEE09}, {0x4A38, 0xEE09}},
    {0x00F414, {0x50F8, 0xEE09}, {0x4A38, 0xEE09}},
};
const std::vector<RomPatch> kInfAmmo = {
    {0x005108, {0x5B78, 0x8F90}, {0x4E71, 0x4E71}}, {0x005130, {0x5378, 0x8F92}, {0x4E71, 0x4E71}},
};
const std::vector<RomPatch> kEndlessQuack = {{0x005FDE, {0x5378, 0x8FA8}, {0x4E71, 0x4E71}}};

// ---- widescreen (docs/re_qs_wide.md) --------------------------------------------------------------
// Keeps map columns [X-96, X+416) in the 512-px planes instead of [X, X+512), so both sides of a
// widened view are valid (same bytes for any E <= 80). Applied in 4:3 too: it only changes which
// off-screen columns the planes hold, so the picture and the game are unchanged, and switching
// widescreen on mid-level (or loading a 4:3 save state in widescreen) finds the columns it needs.
std::vector<RomPatch> stream_patches() {
    const uint16_t off = 96;
    return {
        {0x0026C8, {0x6D0E, 0x3029, 0x0024, 0x3229, 0x0020, 0x4EBA, 0x00B2, 0x6010,
                    0x3029, 0x002C, 0x0640, 0x0200, 0x3229, 0x0020, 0x4EBA, 0x00A0},
                   {0x6D06, 0x3029, 0x0024, 0x6008, 0x3029, 0x002C, 0x0640, 0x0200,
                    0x0440, off, 0x6A02, 0x7000, 0x3229, 0x0020, 0x4EBA, 0x00A0}},
        {0x0026FC, {0x6D0C, 0x3029, 0x0024, 0x3229, 0x0020, 0x6000, 0x0014,
                    0x3029, 0x0024, 0x3229, 0x0028, 0x0641, 0x0100, 0x6000, 0x0004},
                   {0x6D06, 0x3229, 0x0020, 0x6008, 0x3229, 0x0028, 0x0641, 0x0100,
                    0x3029, 0x0024, 0x0440, off, 0x6A02, 0x7000, 0x6002}},
        {0x003FCE, {0x4EBA, 0xE74C}, {0x4EBA, 0xE73C}},
    };
}

std::vector<RomPatch> wide_patches(int E) {
    // Objects are kept alive in world X [camX-256, camX+512); from 2:1 on (right edge camX+384) the
    // right spawn trigger also looks one 128-px cell further so enemies don't appear at the edge.
    const bool far = E > 40;
    const uint16_t keep = far ? 0x0380 : 0x0300;
    std::vector<RomPatch> p = {
        // sprite piece clip: $60-E .. $1C0+E
        // (unsigned tests: the left bound must stay above 0 - beyond E = 95 pieces starting left of VDP X 1
        // are dropped, at most 20 px at the edge)
        {0x009BCE, {0x0C40, 0x0060}, {0x0C40, (uint16_t)std::max(1, 0x60 - E)}},
        {0x009BD4, {0x0C40, 0x01C0}, {0x0C40, (uint16_t)(0x1C0 + E)}},
        // objects: spawn one 128-px cell further left, despawn 256 px off the left edge
        {0x019C1C, {0x0440, 0x0071}, {0x0440, 0x00F1}},
        {0x019D74, {0x0C47, 0x0280}, {0x0C47, keep}},
        {0x009A9A, {0x0C44, 0x0280, 0x6400, 0x1C22, 0x4A44, 0x6B00, 0x1C1C},
                   {0x3004, 0x0640, 0x0080, 0x0C40, keep, 0x6400, 0x1C1C}},
    };
    // Objects that leave and re-enter by the old screen edges, moved E px out so they do it outside the
    // widened view: $3D/$48/$B3 (Hideout bushes and kin) wrap from camX+352 to camX-32; $4F parks at
    // camX+352 for a while before flying in.
    const uint16_t wrap_r = (uint16_t)(0x160 + E), wrap_l = (uint16_t)(0x20 + E);
    for (uint32_t a : {0x010FE4u, 0x011032u, 0x01107Eu, 0x012164u}) p.push_back({a, {0x0640, 0x0160}, {0x0640, wrap_r}});
    for (uint32_t a : {0x010FF4u, 0x011042u, 0x01108Eu}) p.push_back({a, {0x0468, 0x0020, 0x0010}, {0x0468, wrap_l, 0x0010}});
    p.push_back({0x010F12, {0x0469, 0x0020, 0x0010}, {0x0469, wrap_l, 0x0010}});
    p.push_back({0x0110FA, {0x0440, 0x0020}, {0x0440, wrap_l}});
    p.push_back({0x011FEC, {0x0C41, 0x0160}, {0x0C41, wrap_r}});
    p.push_back({0x011FF4, {0x0C41, 0x0140}, {0x0C41, (uint16_t)(0x140 + E)}});
    // Old "far off screen" limits that a wide view reaches: player shots are deleted outside
    // [camX-64, camX+384) ($5956); the type $19 walkers stop beyond screen x 416 / -96, and another walker
    // ($21/$55/$B0) stands still beyond -64 / 384 before turning back. Moved out by E-64: shots go to the
    // edges, the $19 walkers (which wait in plain sight) stay 32 px (a sprite's width) beyond them at 2:1
    // and 21:9. The other walker's limits only move at 21:9 (no visible case; moving them out at 2:1 too
    // changed a $B0's behaviour in The Island Area 1).
    if (E > 64) {
        const uint16_t d = (uint16_t)(E - 64);
        p.push_back({0x005976, {0x0441, 0x0040}, {0x0441, (uint16_t)(0x40 + d)}});
        p.push_back({0x00597E, {0x0641, 0x01C0}, {0x0641, (uint16_t)(0x1C0 + 2 * d)}});
        for (uint32_t a : {0x00CD52u, 0x00CE92u}) p.push_back({a, {0x0C40, 0x01A0}, {0x0C40, (uint16_t)(0x1A0 + d)}});
        for (uint32_t a : {0x00CDB0u, 0x00CF02u}) p.push_back({a, {0x0C40, 0xFFA0}, {0x0C40, (uint16_t)(-0x60 - d)}});
        p.push_back({0x013A6E, {0x0C40, 0xFFC0}, {0x0C40, (uint16_t)(-0x40 - d)}});
        p.push_back({0x013AD6, {0x0C40, 0x0180}, {0x0C40, (uint16_t)(0x180 + d)}});
    }
    if (far) {
        // Right trigger ($19C08, camera X crossing a 128-px cell boundary to the right): the original column
        // (X+$180)>>7 and the far one (X+$200)>>7 both run (wrapper at $7B400). Spawn entries are filed by
        // cell, but some sit cells to the left of theirs (birds that swoop in behind Donald); the far pass
        // would create those in the middle of the view. Entries more than 192 px left of their cell (at 4:3
        // they appear in the left half of the screen or off its left edge) are "deferred": the far pass
        // leaves them and the original-column pass, 128 px later, creates only them - at the 4:3 moment,
        // so also at the same distance from Donald. Entries only a little left of their cell keep the far
        // timing, which brings them in at the right edge as 4:3 does. Entry hook at $7B420.
        // The two passes are told apart by the wrapper's return addresses ($7B40C original, $7B418 far), which
        // nothing else pushes - the area-start spawner ($19D5A) and the left trigger use the same loop.
        const std::vector<uint16_t> wrap = {
            0x3F00, 0x0640, 0x0180, 0x4EB9, 0x0001, 0x9C20,   // move.w d0,-(sp) / addi.w #$180,d0 / jsr $19C20
            0x301F, 0x0640, 0x0200, 0x4EB9, 0x0001, 0x9C20,   // move.w (sp)+,d0 / addi.w #$200,d0 / jsr $19C20
            0x4E75,                                           // rts
        };
        p.push_back({0x07B400, std::vector<uint16_t>(wrap.size(), 0xFFFF), wrap});
        p.push_back({0x019C16, {0x0640, 0x0180, 0x6004}, {0x4EF9, 0x0007, 0xB400}});
        // In $19C5E's entry loop, before the free-slot search: a2 = entry+1 (x at 6(a2)), 6(sp) = column
        // (saved d2), 20(sp) = $19C52 when called from the column loop, 24(sp) = that loop's caller.
        // d3/d6 are free here.
        // The vertical trigger's extra column (X>>7)+4 (`$19BEA` below) leaves deferred entries too: 4:3 never
        // creates them from there, and they would appear inside the view.
        std::vector<uint16_t> hook = {
            0x0CAF, 0x0001, 0x9C52, 0x0014,   //  0 cmpi.l #$19C52,20(sp)   column loop?
            0x6700,                           //  4 beq.s loop
            0x0CAF, 0x0001, 0x9BFC, 0x0014,   //  5 cmpi.l #$19BFC,20(sp)   vertical trigger?
            0x6600,                           //  9 bne.s go
            0x3C2C, 0x0024, 0xEE46, 0x5846,   // 10 move.w $24(a4),d6 / asr.w #7,d6 / addq.w #4,d6
            0xBC6F, 0x0006,                   // 14 cmp.w 6(sp),d6                    its extra column?
            0x6600, 0x6000,                   // 16 bne.s go / bra.s far
            0x0CAF, 0x0007, 0xB40C, 0x0018,   // 18 loop: cmpi.l #$7B40C,24(sp)   original-column pass?
            0x6700,                           // 22 beq.s orig
            0x0CAF, 0x0007, 0xB418, 0x0018,   // 23 cmpi.l #$7B418,24(sp)   far pass?
            0x6600,                           // 27 bne.s go
            0x6100, 0x6B00, 0x6000,           // 28 far: bsr.s deferred / bmi.s skip / bra.s go
            0x7C00, 0x6600,                   // 31 orig: moveq #0,d6 / bne.s go   (SPAWN_ALL_FLAG: #1 = all entries)
            0x6100, 0x6A00,                   // 33 bsr.s deferred / bpl.s skip
            0x47F8, 0x9000, 0x3C3C, 0x002F,   // 35 go: lea $9000.w,a3 / move.w #$2f,d6 (as the original)
            0x4EF9, 0x0001, 0x9D7C,           // 39 jmp $19D7C (free slot search, carry = none)
            0x003C, 0x0001, 0x4E75,           // 42 skip: ori #1,ccr / rts
            0x1C2A, 0x0006, 0xE14E,           // 45 deferred: move.b 6(a2),d6 / lsl.w #8,d6
            0x1C2A, 0x0007,                   // 48 move.b 7(a2),d6                   entry x
            0x362F, 0x000A, 0xEF4B, 0x9C43,   // 50 move.w 10(sp),d3 / lsl.w #7,d3 / sub.w d3,d6
            0x0646, 0x00C0,                   // 54 addi.w #192,d6   (< 0: more than 192 px left of its cell)
            0x4E75,                           // 56 rts
        };
        auto bra = [&](size_t at, size_t target) { hook[at] |= (uint8_t)(2 * ((int)target - (int)at - 1)); };
        bra(4, 18); bra(9, 35); bra(16, 35); bra(17, 28); bra(22, 31); bra(27, 35);
        bra(28, 45); bra(29, 42); bra(30, 35); bra(32, 35); bra(33, 45); bra(34, 42);
        static_assert(SPAWN_ALL_FLAG == 0x07B420 + 31 * 2, "flag word = hook word 31");
        p.push_back({0x07B420, std::vector<uint16_t>(hook.size(), 0xFFFF), hook});
        p.push_back({0x019C92, {0x47F8, 0x9000, 0x3C3C, 0x002F, 0x4EBA, 0x00E0},
                               {0x4EB9, 0x0007, 0xB420, 0x4E71, 0x4E71, 0x4E71}});
        // ...and the vertical trigger spawns 6 cell columns instead of 5 ((X>>7)-1 .. +4), so objects in that
        // extra column also appear when they come into range by scrolling up or down
        p.push_back({0x019BEA, {0x7E04}, {0x7E05}});
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
        0x4EB9, 0x0000, 0x5000, 0x43F8, 0x8500, 0x2229, 0x0004, 0x2411, 0x2602, 0x9681, 0x0C83, need,
        0x0000, 0x6D00, 0x006E, 0x0681, e, 0x0000, 0x0482, e, 0x0000, 0x2029, 0x0024, 0xB081, 0x6E0A,
        0x2001, 0x0680, 0x0001, 0x0000, 0x600E, 0xB082, 0x6D00, 0x0044, 0x2002, 0x0480, 0x0001, 0x0000,
        0x2200, 0x92A9, 0x0024, 0x2340, 0x0024, 0x2340, 0x002C, 0x21C1, 0x8534, 0x42B8, 0x8530, 0x43F8,
        0x8540, 0x4EB9, 0x0000, 0x25E0, 0x2038, 0x8574, 0xD1B8, 0x8564, 0x21F8, 0x8564, 0x856C, 0x42B8,
        0x8574, 0x42B8, 0x8570, 0x42B8, 0x8534, 0x4EF9, 0x0000, 0x3F96,
        // narrow: start at the centre (range 0 = 320-px room: unchanged)
        0x4A83, 0x67F6, 0xE283, 0x2001, 0xD083, 0x60B4,
    };
    p.push_back({0x07B000, std::vector<uint16_t>(code.size(), 0xFFFF), code});
    p.push_back({0x002664, {0x2229, 0x0004, 0xB2A9}, {0x4EB9, 0x0007, 0xB000}});
    p.push_back({0x00266A, {0x0024}, {0x6018}});
    p.push_back({0x0035A0, {0x4EBA, 0x1A5E, 0x4EBA, 0x09F0}, {0x4EB9, 0x0007, 0xB056, 0x4E71}});

    // "On screen" test $E86E (screen x in (-32, 352)): objects that show themselves or are deleted by it use
    // the widened view instead, (-32-E, 352+E) - picked by the caller's return address. Otherwise the
    // Hideout bridges (and their kin) appear piece by piece inside the view, and falling things and
    // dropped items vanish there. Callers that only start an enemy's behaviour keep the original test.
    const uint32_t kWideCallers[] = {
        0x00FB7A,   // bridge / platform pieces $75 $78 $86 $8E: hidden until on screen
        0x00E6EC,   // $0A falls from the top, hides and resets once off screen
        0x00DB60, 0x00DCD0, 0x010832, 0x012258, 0x013F74,   // deleted once off screen
    };
    std::vector<uint16_t> on = {0x3028, 0x0010, 0x3228, 0x0014, 0x9078, 0x8524};
    std::vector<size_t> to_wide;
    for (uint32_t r : kWideCallers) {
        on.insert(on.end(), {0x0C97, (uint16_t)(r >> 16), (uint16_t)r, 0x6700});   // cmpi.l #r,(a7) / beq.s wide
        to_wide.push_back(on.size() - 1);
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
    const size_t ytest = on.size();
    on.insert(on.end(), {0x9278, 0x8520});
    bound(0xFFF0, 0x00E0, 1);
    on.insert(on.end(), {0x7000, 0x4E75});
    const size_t off = on.size();
    on.insert(on.end(), {0x7001, 0x4E75});
    auto branch = [&](size_t at, size_t target) { on[at] |= (uint8_t)(2 * ((int)target - (int)at - 1)); };
    for (size_t at : to_wide) branch(at, wide);
    for (size_t at : to_off) branch(at, off);
    branch(to_y, ytest);
    const uint32_t ON_SCREEN = 0x07B200;
    p.push_back({ON_SCREEN, std::vector<uint16_t>(on.size(), 0xFFFF), on});
    p.push_back({0x00E86E, {0x3028, 0x0010, 0x3228}, {0x4EF9, (uint16_t)(ON_SCREEN >> 16), (uint16_t)ON_SCREEN}});
    return p;
}

bool wide_gameplay(const MegaDrive& md) {
    return (md.ram16(MODE_W) & 0x7FFF) == 5 && md.ram16(0xFFFF1A) == 1;
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

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    MegaDrive& md = *(MegaDrive*)host;
    if (!(wide_gameplay(md) || game_over(md))) return;
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
    const char* rom_hint() const override { return "the QuackShot (World REV 00) ROM"; }
    const char* rom_names() const override { return "QUACKSHOT|ILOVEDONALDDUCK"; }
    uint32_t accent_color() const override { return 0xFFFFA23A; }
    int max_ext() const override { return 108; }  // up to 2.39:1 (536 px); beyond 80 the far columns are drawn from the map

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("inf_lives")) add(kInfLives);
        if (cfg.cheat("inf_health")) add(kInfHealth);
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_ammo")) add(kInfAmmo);
        if (cfg.cheat("endless_quack")) add(kEndlessQuack);
        add(stream_patches());
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, patch_list(cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, patch_list(cfg));
        // From 16:9 or narrower to 2:1 / 21:9 mid-level (a setting, or a narrower save state loaded) the game has
        // created the spawn columns up to (X>>7)+3 only, while the far trigger goes on from +5: until the camera
        // reaches another 128-px cell, the original-column pass creates every entry of its column, not only the
        // deferred ones. module_state[0]: bit 31 + the camera cell at the switch; [1]: width last configured + 1.
        const uint32_t prev = md.module_state[1];
        if (cfg.ext <= 40) md.module_state[0] = 0;
        else if (prev && prev - 1 <= 40) md.module_state[0] = 0x80000000u | (md.ram16(0xFF8524) >> 7);
        md.module_state[1] = (uint32_t)cfg.ext + 1;
        if (md.module_state[0]) md.patch_rom16(SPAWN_ALL_FLAG, 0x7C01);
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
        md.module_state[3] = (uint16_t)view_x;
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
        // The narrow -> wide switch flag (configure) goes off once the camera has reached another 128-px cell -
        // judged by its previous-frame X ($852C): the spawner sees a crossing a frame after $8524 shows it.
        if (md.module_state[0] && (uint32_t)(md.ram16(0xFF852C) >> 7) != (md.module_state[0] & 0xFFFF)) {
            md.module_state[0] = 0;
            if (E > 40) md.patch_rom16(SPAWN_ALL_FLAG, 0x7C00);
        }
        const bool level = wide_gameplay(md) || game_over(md);
        // A switch into play lands mid-frame (the HUD hook still sees the old mode on the status bar
        // lines), so the first frame stays 4:3. module_state[5]: level shown last frame.
        const bool settled = level && (md.module_state[5] & 1);
        md.module_state[5] = level ? 1 : 0;
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
        info.gameplay = settled || cast;
        if (!settled || E == 0) return;
        int x = (int16_t)(uint16_t)md.module_state[3], xmin = md.ram16(0xFF8504), xmax = md.ram16(0xFF8500);
        info.mask_left = std::clamp(E - (x - xmin), 0, E);
        info.mask_right = std::clamp(x + E - xmax, 0, E);
        if (md.vdp.reg[18] == 0x98) {
            info.mask_lines = 192;
            info.pillar_below = !cfg.hud_anchor;
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
        // ending is stopped first.
        if (id == 0x8C) {
            load_bank(md, 0x7A0C4, 0x0F00, 0x6D8);
        } else {
            if (id >= 0xA0 && memcmp(&md.zram[0x0F00], &md.rom[0x7A0C4], 0x6D8) == 0) stop_sound(md);
            load_bank(md, 0x792A6, 0x0DD0, 0xE1E);
        }
        request(md, (uint8_t)id);
    }

    void stop_sound(MegaDrive& md) override { request(md, 0xF9); }


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
