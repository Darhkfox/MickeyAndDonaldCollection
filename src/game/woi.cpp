// World of Illusion Starring Mickey Mouse and Donald Duck (USA) - enhancement module.
// Addresses and techniques: docs/re_woi_game.md, docs/re_woi_wide.md.
#include "game.h"
#include "common.h"
#include "../core/md.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>

namespace {

// RAM
constexpr uint32_t MODE = 0xFFFFD0;       // main mode (bit 15 = initialised), index = & 0x7F
constexpr uint32_t PMODE = 0xFFFFD8;      // bit1 2P, bit0 pad 1 = Mickey
constexpr uint32_t LEVEL = 0xFFFFDA, AREA = 0xFFFFDE, PROGRESS = 0xFFFFE0;
constexpr uint32_t BOARD_STATE = 0xFFA850;
constexpr uint32_t P1 = 0xFFA000, P2 = 0xFFA080;    // player objects
constexpr uint16_t M_NEWGAME = 0x01, M_GAMEPLAY = 0x03, M_PAUSE = 0x0C, M_BOARD = 0x0D, M_ILLUSION_SHOT = 0x0F;

enum : unsigned { V_MICKEY = 1, V_DONALD = 2, V_2P = 4, V_ALL = 7 };
const uint8_t kModeBits[3] = {0x01, 0x00, 0x03};    // 1P Mickey, 1P Donald, 2P (pad 1 Mickey)
const uint16_t kProgress[5] = {0x00, 0x06, 0x0C, 0x16, 0x1D};   // password progress values ($21E7E)

struct Stage { int level, area; unsigned modes; const char* group; const char* name; };
const Stage kStages[] = {
    {1, 0x00, V_ALL, "The Enchanted Forest", "Forest Path"},
    {1, 0x01, V_DONALD, "The Enchanted Forest", "Beanstalk"},
    {1, 0x02, V_MICKEY | V_DONALD, "The Enchanted Forest", "Sunny Woods"},
    {1, 0x03, V_2P, "The Enchanted Forest", "Treehouse"},
    {1, 0x04, V_ALL, "The Enchanted Forest", "Spider Cave"},
    {1, 0x05, V_ALL, "The Enchanted Forest", "Boss - The Giant Spider"},
    {2, 0x00, V_ALL, "The Rocky Mountains", "Cliffs (Magic Carpet)"},
    {2, 0x01, V_DONALD, "The Rocky Mountains", "Underground River"},
    {2, 0x0C, V_MICKEY, "The Rocky Mountains", "Storm Mountain"},
    {2, 0x0F, V_2P, "The Rocky Mountains", "Mountain Cave"},
    {2, 0x10, V_ALL, "The Rocky Mountains", "Above the Clouds"},
    {2, 0x11, V_ALL, "The Rocky Mountains", "Boss - Stone Door Cave"},
    {3, 0x00, V_ALL, "Under the Sea", "Open Sea"},
    {3, 0x0A, V_ALL, "Under the Sea", "Open Sea (Deep)"},
    {3, 0x01, V_ALL, "Under the Sea", "Coral Temple"},
    {3, 0x03, V_DONALD, "Under the Sea", "Tropical Island"},
    {3, 0x04, V_MICKEY, "Under the Sea", "Pink Coral Reef"},
    {3, 0x05, V_2P, "Under the Sea", "Ice Block Ruins"},
    {3, 0x06, V_ALL, "Under the Sea", "Open Sea 2"},
    {3, 0x07, V_ALL, "Under the Sea", "Sunken Ship"},
    {3, 0x08, V_ALL, "Under the Sea", "Boss - Inside the Ship"},
    {3, 0x09, V_ALL, "Under the Sea", "After the Boss"},
    {4, 0x00, V_MICKEY | V_2P, "The Giant House", "House Hub"},
    {4, 0x01, V_DONALD, "The Giant House", "House Hub (Donald)"},
    {4, 0x08, V_DONALD, "The Giant House", "Storybook Forest"},
    {4, 0x0C, V_2P, "The Giant House", "Toy Box"},
    {4, 0x0F, V_MICKEY, "The Giant House", "Candy Land"},
    {4, 0x0A, V_MICKEY, "The Giant House", "Flying Over the Sky"},   // Donald has no flying animation
    {4, 0x10, V_ALL, "The Giant House", "Boss - The Candy Witch"},
    {4, 0x11, V_ALL, "The Giant House", "The Kitchen"},
    {5, 0x00, V_ALL, "The Magician's Castle", "Magic Garden"},
    {5, 0x1A, V_ALL, "The Magician's Castle", "Falling Through the Forest"},
    {5, 0x01, V_DONALD, "The Magician's Castle", "Hedge Maze"},
    {5, 0x0B, V_MICKEY, "The Magician's Castle", "Palace Hall"},
    {5, 0x0D, V_2P, "The Magician's Castle", "Hall of Doors"},
    {5, 0x17, V_ALL, "The Magician's Castle", "Starry Stage 1"},
    {5, 0x18, V_ALL, "The Magician's Castle", "Starry Stage 2"},
    {5, 0x19, V_ALL, "The Magician's Castle", "Final Boss - The Magician"},
};

const SoundEntry kSounds[] = {
    {0x82, "Title", true}, {0x83, "The Story Book", true}, {0x99, "Treasure Chest", true},
    {0x84, "The Enchanted Forest", true}, {0x85, "Spider Cave", true}, {0x86, "The Rocky Mountains", true},
    {0x87, "Storm Mountain", true}, {0x88, "Underground River", true}, {0x89, "Mountain Cave", true},
    {0x8A, "Under the Sea", true}, {0x8B, "Tropical Island", true}, {0x8C, "Sunken Ship", true},
    {0x8D, "The Giant House", true}, {0x8E, "Toy Box", true}, {0x8F, "Candy Land", true},
    {0x90, "Flying Over the Sky", true}, {0x94, "The Magic Garden", true}, {0x96, "Palace Hall", true},
    {0x95, "Hall of Doors", true}, {0x9D, "Starry Stage", true}, {0x97, "Boss Battle", true},
    {0x98, "Final Boss", true}, {0x9B, "Ending I", true}, {0x9C, "Ending II", true},
    {0x9E, "Ending III", true}, {0x9F, "Ending & Staff Roll", true}, {0x9A, "Game Over", true},
    // ($81 is an empty stub: only a DAC track with no notes, so it is left out.)
    {0xA0, "Item Pickup", false}, {0xA1, "Cape Attack", false}, {0xA5, "Jump", false},
    {0xAB, "Jump 2", false}, {0xA7, "Fall", false}, {0xAD, "Bumped", false},
    {0xBD, "Pause", false}, {0xBF, "Enemy", false}, {0xC3, "Card Flip", false},
    {0xDD, "Route Switch", false}, {0xD3, "Mickey: Alakazam!", false}, {0xD4, "Donald: Alakazam!", false},
    {0xD5, "Mickey: Ouch", false}, {0xD6, "Donald: Ouch", false}, {0xDE, "Mickey: Voice", false},
    {0xDF, "Donald: Voice", false}, {0xE8, "Magic Spell", false}, {0xE9, "Magic Spell 2", false},
};

// Cheats (docs section 4.2) - apply to both players.
const std::vector<RomPatch> kInvincible = {{0x00F026, {0x6618}, {0x6018}}};
const std::vector<RomPatch> kInfHealth = {{0x00F098, {0x9F28, 0x0038}, {0x4A28, 0x0038}},
                                          {0x00F0D0, {0x9F28, 0x0038}, {0x4A28, 0x0038}}};
const std::vector<RomPatch> kInfLives = {{0x00F0EE, {0x5378, 0xFD52}, {0x4A78, 0xFD52}}};

// ---- widescreen (docs/re_woi_wide.md) --------------------------------------------------------------
// Plane streaming already covers [camX-96, camX+401). The patches widen the sprite clip, object
// despawn/spawn windows, map animations and the 2-player / auto-scroll "keep on screen" rules
// (new code in the $FF padding at $0FFC00). Near a level edge the renderer shifts the widened view
// inwards by up to E px (VDP::view_shift) instead of blacking out the outside, so the sprite clip and
// map animations cover +-2E around the original view (sprite X past $1FF uses the 10-bit X option).
constexpr uint32_t T_SPAWN = 0x0FFC00, T_ANIM = 0x0FFC40;

std::vector<RomPatch> wide_patches(int E) {
    const uint16_t e = (uint16_t)E;
    auto ff = [](size_t n) { return std::vector<uint16_t>(n, 0xFFFF); };
    std::vector<uint16_t> spawn = {0x302C, 0x0010, 0x362C, 0x0020, 0x3400, 0x9443, 0x6B0A, 0x0640, e,
                                   0x0643, e, 0x6008, 0x0440, e, 0x0443, e, 0xB143, 0x0803, 0x0007,
                                   0x6706, 0x4EF9, 0x0002, 0x0A7E, 0x4E75};
    std::vector<uint16_t> anim = {0x3438, 0x8510, 0x0442, (uint16_t)(2 * E), 0xE842, 0x6A02, 0x7400, 0x4E75};
    const uint16_t k = (uint16_t)(20 + (4 * E + 15) / 16);
    return {
        // sprite piece clip (+-2E for the shifted view) and object despawn window. The tests are unsigned,
        // so the left bound stays at VDP X 1 or more (X 0 would also act as a sprite mask): at 2:1 pieces
        // starting left of screen x -127 are dropped (only at the very left edge, near level ends).
        {0x00BBEC, {0x0C40, 0x0060}, {0x0C40, (uint16_t)std::max(1, 0x60 - 2 * E)}},
        {0x00BBF2, {0x0C40, 0x01C0}, {0x0C40, (uint16_t)(0x1C0 + 2 * E)}},
        {0x00BAB6, {0x0C44, 0x0280, 0x6400, 0x3B44, 0x4A44, 0x6B00, 0x3B3E, 0x0C43, 0x0280, 0x6400, 0x3B36, 0x4A43, 0x6B00, 0x3B30},
                   {0x0644, e, 0x0C44, (uint16_t)(0x280 + 2 * E), 0x6400, 0x3B40, 0x0444, e, 0x0C43, 0x0280, 0x6400, 0x3B34, 0x4E71, 0x4E71}},
        // spawning relative to the widened edges
        {0x0209FA, {0x302C, 0x0010, 0x362C, 0x0020, 0x3400}, {0x4EF9, 0x000F, (uint16_t)T_SPAWN, 0x4E71, 0x4E71}},
        {T_SPAWN, ff(spawn.size()), spawn},
        {0x020A4A, {0x302C, 0x0010, 0x0240, 0xFF80, 0x3400}, {0x342C, 0x0010, 0x0442, e, 0x4E71}},
        {0x020A60, {0x7E04}, {0x7E05}},
        {0x020BD6, {0x0C47, 0x0280}, {0x0C47, 0x0300}},
        // triggered map animations
        {0x00ADE6, {0x3438, 0x8510, 0xE842}, {0x4EB9, 0x000F, (uint16_t)T_ANIM}},
        {T_ANIM, ff(anim.size()), anim},
        {0x00ADF6, {0x0642, 0x0014}, {0x0642, k}},
        // 2P keep-both-on-screen rule and the W5 auto-scroll push window
        {0x007D4A, {0x0640, 0x0010}, {0x0640, (uint16_t)(0x10 - E)}},
        {0x007D52, {0x0640, 0x0120}, {0x0640, (uint16_t)(0x120 + 2 * E)}},
        {0x0090B4, {0x0640, 0x0010}, {0x0640, (uint16_t)(0x10 - E)}},
        {0x0090BC, {0x0640, 0x0128}, {0x0640, (uint16_t)(0x128 + 2 * E)}},
        {0x0090E2, {0x0640, 0x0010}, {0x0640, (uint16_t)(0x10 - E)}},
        {0x0090EA, {0x0640, 0x0128}, {0x0640, (uint16_t)(0x128 + 2 * E)}},
        // W4 act 8 object that appeared at the old right edge
        {0x01B4B8, {0x0640, 0x0140}, {0x0640, (uint16_t)(0x140 + E)}},
        // type 43 stays hidden until it is within camX+352 (inside the view from 2:1 on)
        {0x0129FE, {0x0640, 0x0160}, {0x0640, (uint16_t)(0x160 + E)}},
    };
}

bool wide_active(const MegaDrive& md) {
    uint16_t m = md.ram16(MODE) & 0x7F;
    return m == 2 || m == 3 || m == 10 || m == 12;
}

// module_state: [0] header X min, [1] header X max, [2] (level << 8 | area) of the snapshot + 1,
// [3] HUD shift in pixels (positive = towards the left edge), [4] current view shift.
void camera_bounds(const MegaDrive& md, int& lo, int& hi) {
    uint32_t key = ((uint32_t)md.ram16(LEVEL) << 8 | md.ram16(AREA)) + 1;
    if (md.module_state[2] == key) { lo = (int)md.module_state[0]; hi = (int)md.module_state[1]; }
    else { lo = (int16_t)md.ram16(0xFF8528); hi = (int16_t)md.ram16(0xFF852C); }
}

// HUD sprites stay fixed on the output: left-anchored, "PAUSE" centred (compensating the view shift).
int hud_sprite_xoff(void* host, int index, uint16_t attr, int x, int y) {
    (void)attr; (void)x; (void)y;
    MegaDrive& md = *(MegaDrive*)host;
    if (index > 9 || md.ram16(0xFFFFD2) > 1 || !wide_active(md)) return 0;
    int vs = (int)(int32_t)md.module_state[4];
    return index == 2 || index == 3 ? vs : vs - (int)md.module_state[3];
}

// At VBlank (when the frame's scroll values are uploaded) work out the view shift and edge masks
// from the same camera position the game just used.
// Plane B's speed relative to the camera in eighths: its camera follows plane A's moves times the $FCC0 factor
// ($7F06: 0 none, 1/2 half, 3 full, 4 double, 5 vertical only).
int plane_b_speed8(const MegaDrive& md) {
    switch (md.ram16(0xFFFCC0)) {
    case 1: case 2: return 4;
    case 3: return 8;
    case 4: return 16;
    default: return 0;
    }
}

// Background bands that move at their own speed (per-line scroll written by the section's code), measured
// with tools/qa/woi21/band_survey.py: section, plane B world Y range, speed in eighths of the camera's;
// prev_b: the band follows plane B's camera of the previous frame (its line values are written from it).
struct Band { uint16_t level, area; int y0, y1; int r8; bool prev_b; };
const Band kBands[] = {
    {1, 0x00, 944, 1007, 10},   // Forest Path: low band at 1.25x
    {3, 0x04, 720, 759, 10},    // Pink Coral Reef: the same kind of band
    {3, 0x00, 0, 15, 1},        // Open Sea: cloud rows
    {3, 0x00, 16, 23, 0},
    {3, 0x00, 24, 31, 3},
    {3, 0x00, 392, 399, 2},
    {4, 0x0F, 992, 1023, 0, true},   // Candy Land: the animated cream band at the bottom
    // (Palace Hall's floor, Y 208-255 in area $0C, barely moves either, but it is a 320-px-wide picture tied to
    // the screen: held still, the wide view shows its empty plane row - so it keeps plane B's slide.)
};

// next_frame: called at VBlank for the next frame (advances the previous-frame history); false when only
// re-applying the view for the frame about to be drawn (after configure / a state load).
void update_view(MegaDrive& md, bool next_frame = false) {
    VDP& v = md.vdp;
    const int E = v.ext;
    if (!wide_active(md) || E == 0) {
        v.view_shift = v.edge_mask_l = v.edge_mask_r = 0;
        v.view_shift_b_on = false;
        md.module_state[4] = 0;
        md.module_state[7] = 0;
        return;
    }
    int lo, hi;
    camera_bounds(md, lo, hi);
    // The shift follows the camera without the screen shake ($FFFCCC), so the shake stays visible;
    // the masks use the shaken picture.
    const int cam = (int16_t)md.ram16(0xFF8510);
    const int x = cam - (int16_t)md.ram16(0xFFFCCC);
    int vs;
    if (hi - lo >= 2 * E) {
        if (cam < lo + E) vs = lo + E - cam;       // near the left edge: show more on the right
        else if (cam > hi - E) vs = hi - E - cam;  // near the right edge: show more on the left
        else vs = 0;
    } else {
        vs = (lo + hi) / 2 - cam;                  // narrow room: centre it, mask the rest
    }
    vs = std::clamp(vs, -E, E);
    int left = x - E + vs, right = x + 320 + E + vs;   // world X range shown
    v.view_shift = vs;
    // Plane B moves at a fraction of the camera's speed: shifting it by the same fraction of vs keeps it still
    // while the view slides (as plane A is), instead of drifting against its normal direction.
    // The main lines follow plane B's camera, kept in 16.16 ($8570; the scroll uses its integer part): taking the
    // integer part of the shifted fractional position (not shifting by a rounded amount) keeps them exactly still,
    // without a 1-px back-and-forth, and gives 0 when there is no shift.
    const int by = (int16_t)md.ram16(0xFF8598);
    const int r8 = plane_b_speed8(md);
    const int32_t bx = (int32_t)md.ram32(0xFF8570);
    const int main_shift = (int)(((int64_t)bx + (int64_t)vs * r8 * 8192) >> 16) - (bx >> 16);
    // The same for last frame's plane B camera and shift (module_state[5], [6]; [7] = valid).
    const bool have_prev = md.module_state[7] != 0;
    const int32_t pbx = have_prev ? (int32_t)md.module_state[5] : bx;
    const int pvs = have_prev ? (int)(int32_t)md.module_state[6] : vs;
    const int prev_shift = (int)(((int64_t)pbx + (int64_t)pvs * r8 * 8192) >> 16) - (pbx >> 16);
    const uint16_t level = md.ram16(LEVEL), area = md.ram16(AREA);
    // Bands are set from the integer camera as ceil(camX * speed) (measured: the 1.25x bands), so shifting
    // by the difference of that at the shifted camera keeps them exactly still too.
    auto ceil8 = [](int x, int r8) { const int t = x * r8; return t >= 0 ? (t + 7) / 8 : -((-t) / 8); };
    for (int ln = 0; ln < VDP::kMaxLines; ln++) {
        int s = main_shift;
        for (const Band& b : kBands)
            if (b.level == level && b.area == area && by + ln >= b.y0 && by + ln <= b.y1)
                s = b.prev_b ? prev_shift : ceil8(cam + vs, b.r8) - ceil8(cam, b.r8);
        v.view_shift_b[ln] = (int16_t)s;
    }
    v.view_shift_b_on = true;
    v.edge_mask_l = std::clamp(lo - left, 0, E);
    v.edge_mask_r = std::clamp(right - (hi + 320), 0, E);
    md.module_state[3] = (v.hook_flags & 1) ? (uint32_t)std::clamp((hi - lo) / 2, 0, E) : 0;
    md.module_state[4] = (uint32_t)vs;
    if (next_frame) {
        md.module_state[5] = (uint32_t)bx;
        md.module_state[6] = (uint32_t)vs;
        md.module_state[7] = 1;
    }
}

void wide_vblank(MegaDrive& md) {
    if (md.vdp.ext && wide_active(md) && md.ram8(0xFFFC60) == 0) return;   // lag frame: the game keeps the old scroll values
    update_view(md, true);
}

uint8_t bus8(const MegaDrive& md, uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) return md.ram8(a);
    return a < md.rom.size() ? md.rom[a] : 0;
}
uint16_t bus16(const MegaDrive& md, uint32_t a) { return (uint16_t)(bus8(md, a) << 8 | bus8(md, a + 1)); }

// The planes hold map columns [camX-96, camX+401) (docs §2). Shifted by up to E near level edges, the
// view covers [camX-2E, camX+320+2E): beyond E = 40 that passes the streamed columns, so cells outside
// them are drawn here straight from the level map, with the game's own lookup (column writer $810A:
// layout of 256-px chunks, 32 per row -> 16x16 block indices -> 4 name-table words per block) and the
// H/V scroll the VDP used for this line. Planes whose columns the section doesn't stream ($FC61 bit 5 A,
// bit 7 B) hold their whole 512-px picture and need nothing.
void draw_far_columns(const MegaDrive& md, int line, uint8_t* buf, bool plane_b) {
    const VDP& v = md.vdp;
    const uint32_t L = plane_b ? 0xFF8560 : 0xFF8500;
    const int E = v.ext, TW = 320 + 2 * E;
    const int vs = plane_b && v.view_shift_b_on ? v.view_shift_b[line] : v.view_shift;
    const int camx = (int16_t)md.ram16(L + 0x10), camy = (int16_t)md.ram16(L + 0x38);
    const uint32_t layout = md.ram32(L), blocks = md.ram32(L + 4), tiles = md.ram32(L + 8);
    const uint32_t hsb = (v.reg[13] & 0x3F) << 10;
    int hoff;
    switch (v.reg[11] & 3) {
    case 0: hoff = 0; break;
    case 1: hoff = (line & 7) * 4; break;
    case 2: hoff = (line & ~7) * 4; break;
    default: hoff = line * 4; break;
    }
    const int px0 = -(v.vram16(hsb + hoff + (plane_b ? 2 : 0)) & 0x3FF) & 0x1FF;
    const int wx0 = camx + (((px0 - camx) + 256) & 0x1FF) - 256;        // world x of the game's screen x 0
    const int py = (line + (v.vsram[plane_b ? 1 : 0] & 0x3FF)) & 0xFF;  // 256-px tall plane
    const int wy = camy + line + (((py - (camy + line)) + 128) & 0xFF) - 128;
    if (wy < 0) return;
    // Judge what the plane holds by the scroll shown on this line; camX in RAM may already be the next frame's.
    const int first = std::max(wx0 - 96, 0) >> 4;
    const int lo = (first + 1) * 16, hi = (first + 31) * 16;            // columns the plane surely holds
    uint8_t row[8];
    for (int x = 0; x < TW;) {
        const int wx = wx0 + x - E + vs;
        if (wx >= lo && wx < hi) { x += hi - wx; continue; }
        const int n = std::min(8 - (wx & 7), TW - x);
        if (wx >= 0) {
            const uint8_t chunk = bus8(md, layout + (int16_t)((wx >> 8) + ((wy >> 8) << 5)));
            const int off = (int16_t)((chunk << 9) + ((wy >> 4) & 15) * 32 + ((wx >> 4) & 15) * 2);
            const uint32_t block = bus16(md, blocks + off) & 0x1FF;
            const uint16_t word = bus16(md, tiles + block * 8 + ((wy & 8) ? 4 : 0) + ((wx & 8) ? 2 : 0));
            v.pattern_row(word, wy & 7, row);
            memcpy(buf + x, row + (wx & 7), n);
        }
        x += n;
    }
}

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    MegaDrive& md = *(MegaDrive*)host;
    if (md.vdp.ext <= 40 || !wide_active(md)) return;
    const uint8_t no_stream = md.ram8(0xFFFC61);
    if (!(no_stream & 0x20)) draw_far_columns(md, line, plane_a, false);
    if (!(no_stream & 0x80)) draw_far_columns(md, line, plane_b, true);
}

bool hud_sprite(void* host, int index) {
    MegaDrive& md = *(MegaDrive*)host;
    return index <= 9 && md.ram16(0xFFFFD2) <= 1;
}

class WorldOfIllusion : public GameModule {
public:
    const char* id() const override { return "woi"; }
    const char* title() const override { return "World of Illusion"; }
    const char* subtitle() const override { return "Starring Mickey Mouse & Donald Duck"; }
    const char* year() const override { return "1992"; }
    int max_players() const override { return 2; }
    uint32_t rom_crc32() const override { return 0x921EBD1C; }
    uint32_t rom_size() const override { return 0x100000; }
    const char* rom_hint() const override { return "the World of Illusion (USA) ROM"; }
    const char* rom_names() const override { return "WORLDOFILLUSION|MICKEYDONALD"; }
    uint32_t accent_color() const override { return 0xFF4FC3F7; }
    float audio_gain() const override { return 1.4f; }   // its driver mixes ~3-5 dB below COI/QS
    int max_ext() const override { return 64; }   // up to 2:1; beyond 40 the far columns are drawn from the map

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_health")) add(kInfHealth);
        if (cfg.cheat("inf_lives")) add(kInfLives);
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, patch_list(cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, patch_list(cfg));
        md.vdp.hook_host = &md;
        md.vdp.sprite_xoff = cfg.ext > 0 ? hud_sprite_xoff : nullptr;
        md.vdp.sprite_is_hud = cfg.ext > 0 ? hud_sprite : nullptr;
        md.vdp.sprite_x10 = cfg.ext > 0;
        md.vdp.line_hook = cfg.ext > 40 ? line_hook : nullptr;
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
        md.on_vblank = cfg.ext > 0 ? wide_vblank : nullptr;
        // Right away, not at the next VBlank: after a state load the next frame is drawn first.
        update_view(md);
    }

    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        if (!cfg.cheat("max_hearts") || (md.ram16(MODE) & 0x7F) != M_GAMEPLAY) return;
        // Top hearts up to the maximum of 8 for each active, living player.
        for (uint32_t obj : {P1, P2}) {
            if (obj == P2 && !(md.ram8(PMODE) & 2)) break;
            if (md.ram8(obj + 0x38) != 0 && md.ram8(obj + 0x6D) == 0) md.set_ram8(obj + 0x38, 8);
        }
    }

    void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        info.gameplay = wide_active(md);
        // Snapshot the section's camera bounds while it loads, before level scripts lock the camera
        // (also in 4:3, so switching widescreen on mid-level has them).
        if ((md.ram16(MODE) & 0x7F) == 2) {
            md.module_state[0] = (uint32_t)(int16_t)md.ram16(0xFF8528);
            md.module_state[1] = (uint32_t)(int16_t)md.ram16(0xFF852C);
            md.module_state[2] = ((uint32_t)md.ram16(LEVEL) << 8 | md.ram16(AREA)) + 1;
        }
        if (E == 0) return;
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies and hazards can't hurt Mickey or Donald. Bottomless pits still cost a try."},
            {"inf_health", "Infinite Hearts", "Hits knock the players back but never remove hearts."},
            {"inf_lives", "Infinite Tries", "Losing a life never reduces the shared TRIES counter."},
            {"max_hearts", "Maximum Hearts", "Both players are kept at the full 8 hearts."},
        };
    }

    std::vector<std::string> level_variants() const override { return {"Mickey", "Donald", "2 Players"}; }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Stage& s : kStages) v.push_back({s.group, s.name, s.modes});
        return v;
    }

    bool start_level(MegaDrive& md, const GameConfig& cfg, int index, int variant) override {
        if (index < 0 || index >= (int)(sizeof(kStages) / sizeof(kStages[0]))) return false;
        if (variant < 0 || variant > 2) variant = 0;
        const Stage& st = kStages[index];
        md.power_on();
        configure(md, cfg);
        auto mode = [&]() { return md.ram16(MODE); };
        // power-on -> ... -> options card board (tap START every 20 frames)
        for (int i = 0; i < 200 && mode() != (0x8000 | M_BOARD); i++) {
            md.set_pad(0, MD_START);
            md.run_frame();
            md.run_frame();
            md.set_pad(0, 0);
            for (int f = 0; f < 18 && mode() != (0x8000 | M_BOARD); f++) md.run_frame();
        }
        if (mode() != (0x8000 | M_BOARD)) return false;
        if (!run_until(md, [&]() { return md.ram16(BOARD_STATE) == 1; }, 600)) return false;
        for (int i = 0; i < 30; i++) md.run_frame();
        // START on the board; on the first frame of the new-game init set mode / level / area.
        md.set_pad(0, MD_START);
        bool reached = false;
        for (int i = 0; i < 200; i++) {
            md.run_frame();
            md.set_pad(0, 0);
            if (mode() == M_NEWGAME) { reached = true; break; }
            if ((mode() & 0x7F) != M_BOARD) return false;
        }
        if (!reached) return false;
        md.set_ram8(PMODE, (uint8_t)((md.ram8(PMODE) & 0xFC) | kModeBits[variant]));
        md.set_ram16(LEVEL, (uint16_t)st.level);
        md.set_ram16(AREA, (uint16_t)st.area);
        md.set_ram16(PROGRESS, kProgress[st.level - 1]);
        return run_until(md, [&]() { return (mode() & 0x7F) == M_GAMEPLAY; }, 1500);
    }

    std::vector<SoundEntry> sounds() const override {
        std::vector<SoundEntry> v(std::begin(kSounds), std::end(kSounds));
        for (int id = 0xA0; id <= 0xDF; id++) {
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
        // The driver is uploaded by the SEGA-logo mode; park on the silent "Illusion Shot" screen.
        run_until(md, [&]() { return (md.ram16(MODE) & 0x7F) == M_ILLUSION_SHOT; }, 1200);
        for (int i = 0; i < 20; i++) md.run_frame();
        freeze_68k(md);
        stop_sound(md);
        for (int i = 0; i < 4; i++) md.run_frame();
    }

    void play_sound(MegaDrive& md, int id) override { request(md, (uint8_t)id); }
    void stop_sound(MegaDrive& md) override {
        // The driver keeps one request per frame by priority: a stop must not share the mailbox.
        for (uint16_t slot = 0x1C0A; slot <= 0x1C0C; slot++) md.zram[slot] = 0;
        request(md, 0xEB);
    }

private:
    static void request(MegaDrive& md, uint8_t id) {
        for (uint16_t slot = 0x1C0A; slot <= 0x1C0C; slot++)
            if (md.zram[slot] == 0) { md.zram[slot] = id; return; }
        md.zram[0x1C0A] = id;
    }
};

}  // namespace

GameModule* create_woi_module() { return new WorldOfIllusion(); }
