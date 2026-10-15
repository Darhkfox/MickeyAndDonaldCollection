// Castle of Illusion Starring Mickey Mouse (USA) - enhancement module.
// Addresses and techniques: docs/re_coi_game.md, docs/re_coi_wide.md.
#include "game.h"
#include "common.h"
#include "../core/md.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace {

// RAM
constexpr uint32_t MODE = 0xFFF300;       // main-loop mode (handler index * 2)
constexpr uint32_t PAD_HELD = 0xFFF308;
constexpr uint32_t LEVEL = 0xFFFF42;
constexpr uint32_t ITEMS = 0xFFF320;
constexpr uint32_t HUD_DIRTY = 0xFFF409;  // bit2: redraw items
constexpr uint32_t HUD_ITEMS = 0xFFF238;  // two HUD digit tiles for ITEMS
constexpr uint16_t M_OPTIONS = 0x0A, M_LEVEL_INIT = 0x0C, M_GAMEPLAY = 0x0E, M_WARP = 0x10;
// Z80 sound driver request slots
constexpr uint16_t Z_MUSIC = 0x1C0C, Z_SFX = 0x1C0A, Z_SFX2 = 0x1C0B;

struct Level { int index; const char* group; const char* name; };
// Play order (round 4's tea-cup side paths listed after the main route).
const Level kLevels[] = {
    {0x00, "The Enchanted Forest", "Act 1 - Forest Floor"},
    {0x02, "The Enchanted Forest", "Act 1 - Walking Trees"},
    {0x03, "The Enchanted Forest", "Act 1 - Spider Web Cavern"},
    {0x01, "The Enchanted Forest", "Act 2 - Tree Tops"},
    {0x04, "The Enchanted Forest", "Boss - The Evil Tree"},
    {0x05, "Toyland", "Act 1 - Toy Room"},
    {0x06, "Toyland", "Act 2 - Upside-Down Room"},
    {0x07, "Toyland", "Act 3 - Toy Soldiers"},
    {0x08, "Toyland", "Boss"},
    {0x09, "The Storm", "Act 1 - Storm Cliffs"},
    {0x0A, "The Storm", "Act 2 - The Ruins"},
    {0x0B, "The Storm", "Act 3 - Deep Ruins"},
    {0x0C, "The Storm", "Boss"},
    {0x0D, "The Library", "Act 1 - Bookshelves"},
    {0x0E, "The Library", "Act 1 - Milk Bottle Shelf"},
    {0x12, "The Library", "Inside Tea Cup 1"},
    {0x13, "The Library", "Inside Tea Cup 2"},
    {0x14, "The Library", "Inside Tea Cup 3"},
    {0x0F, "The Library", "Shelves after Tea Cup 1"},
    {0x10, "The Library", "Shelves after Tea Cup 2"},
    {0x11, "The Library", "Shelves after Tea Cup 3"},
    {0x15, "The Library", "Act 2 - Dessert Land"},
    {0x16, "The Library", "Boss"},
    {0x17, "The Clock Tower", "Act 1 - Castle Hall"},
    {0x18, "The Clock Tower", "Act 1 - Tower Stairs"},
    {0x19, "The Clock Tower", "Act 2 - Gears"},
    {0x1A, "The Clock Tower", "Boss - The Jester"},
    {0x1B, "Castle of Illusion", "Final Battle - Mizrabel"},
};

const SoundEntry kSounds[] = {
    {0x81, "Once Upon a Mouse (Title)", true},
    {0x9E, "The Castle", true},
    {0x82, "The Enchanted Forest", true},
    {0x83, "Spider Web Cavern", true},
    {0x84, "Forest Tree Tops", true},
    {0x9F, "Boss Battle", true},
    {0x99, "Boss Defeated", true},
    {0x89, "Round Clear", true},
    {0x87, "Toyland", true},
    {0x88, "Toyland (Part 2)", true},
    {0x8B, "The Storm", true},
    {0x8C, "The Ruins", true},
    {0x8E, "The Library", true},
    {0x91, "Dessert Land", true},
    {0x96, "The Clock Tower", true},
    {0x98, "Clock Tower Gears", true},
    {0x9A, "Mizrabel", true},
    {0x9B, "Mizrabel's Illusion", true},
    {0x90, "Ending & Credits", true},
    {0x8A, "Lost a Try", true},
    {0x9D, "Game Over", true},
    {0x93, "Special Item", true},
    {0x86, "Secret Theme 1", true},
    {0x94, "Secret Theme 2", true},
    {0x85, "Secret Theme 3", true},
    {0x9C, "Unused Theme", true},
    {0x92, "Unused Jingle", true},
    {0xA0, "Throw", false}, {0xA1, "Mickey Move", false}, {0xA2, "Jump", false},
    {0xA3, "Hurt", false}, {0xA4, "Unused 1", false}, {0xA5, "Transform", false},
    {0xA6, "Crushed", false}, {0xA7, "Hazard", false}, {0xA8, "Enemy Defeated", false},
    {0xA9, "Enemy 1", false}, {0xAA, "Boss Appears", false}, {0xAB, "Enemy 2", false},
    {0xAC, "Enemy 3", false}, {0xAD, "Grab", false}, {0xAE, "Enemy 4", false},
    {0xAF, "Unused 2", false}, {0xB0, "Bonus", false}, {0xB1, "Recover", false},
    {0xB2, "Projectile", false}, {0xB3, "Enemy 5", false}, {0xB4, "Fall", false},
    {0xB5, "Enemy 6", false}, {0xB6, "Enemy 7", false}, {0xB7, "Enemy 8", false},
    {0xB8, "Enemy 9", false}, {0xB9, "Magic", false}, {0xBA, "Unused 3", false},
    {0xBB, "Enemy 10", false}, {0xBC, "Boss Hit 1", false}, {0xBE, "Boss Hit 2", false},
    {0xBF, "Unused 4", false}, {0xC0, "Item Pickup", false}, {0xC1, "Pause", false},
    {0xC2, "Warp", false}, {0xC3, "Special Item", false}, {0xC4, "Extra Try", false},
    {0xC5, "Object", false}, {0xC6, "Unused 5", false}, {0xC8, "Raster Effect", false},
};

// Cheats (all verified ROM patches, docs/re_coi_game.md section 3.3)
const std::vector<RomPatch> kInvincible = {
    {0x0092B4, {0x4A38}, {0x4E75}}, {0x0092D4, {0x4A38}, {0x4E75}},
    {0x00B526, {0x6604}, {0x6004}}, {0x00BF74, {0x6604}, {0x6004}},
};
const std::vector<RomPatch> kInfPower = {{0x0092F4, {0x5378, 0xF322}, {0x4E71, 0x4E71}}};
const std::vector<RomPatch> kInfTries = {
    {0x009300, {0x5378, 0xF324}, {0x4E71, 0x4E71}}, {0x00BD6E, {0x5378, 0xF324}, {0x4E71, 0x4E71}},
};
const std::vector<RomPatch> kInfItems = {{0x00B8CA, {0x5378, 0xF320}, {0x4E71, 0x4E71}}};
const std::vector<RomPatch> kInfContinues = {{0x07A590, {0x5338, 0xFF40}, {0x4E71, 0x4E71}}};
// max(power, 3) -> max(power, 5) at round starts (Hard: 2 -> 5), and don't skip it after an act exit ($970C beq -> bra;
// the Hard path has no act-exit test and still keeps the power there, like the original).
const std::vector<RomPatch> kMaxPower = {
    {0x00972A, {0x7003}, {0x7005}}, {0x009746, {0x0002}, {0x0005}}, {0x00970C, {0x6718}, {0x6018}},
};
const std::vector<RomPatch> kDebugSelect = {{0x009F6C, {0x4E75}, {0x4E71}}};

// ---- widescreen (docs/re_coi_wide.md) --------------------------------------------------------------
// Tile streaming needs no change (the engine keeps the whole 512-px plane valid). The patches widen
// sprite culling, the object spawn/delete windows, the animated-block visibility test, the SFX
// "on screen" test, and clamp the camera E px inside each level (code caves in the $FF fill at the end).
constexpr uint32_t CAVE_H = 0x7FE80, CAVE_V = 0x7FE9A, CAVE_CAM = 0x7FEB0, CAVE_ANIM = 0x7FF40;

std::vector<RomPatch> wide_patches(int E) {
    // k = animated-block look-ahead in 16-px blocks. More than 3 overlaps the plane columns the
    // streamer is rewriting (Toyland 2 corrupts at E >= 56), so the 2:1 mode keeps 3.
    const uint16_t e = (uint16_t)E, k = (uint16_t)((std::min(E, 48) + 15) / 16);
    const uint16_t up = (uint16_t)std::max(0x1E0, 0x1C0 + E);
    // The clip tests are unsigned, so the left bound must stay above 0 (X = 0 would also act as a
    // sprite mask); beyond E = 95 pieces starting left of VDP X 1 are dropped (at most 20 px at the edge).
    const uint16_t lo = (uint16_t)std::max(1, 0x60 - E);
    auto ff = [](size_t n) { return std::vector<uint16_t>(n, 0xFFFF); };
    std::vector<uint16_t> cave_h = {0x323C, e, 0x4A6E, 0x001E, 0x6A02, 0x4441, 0x302E, 0x0004, 0xD041,
                                    0x362E, 0x0018, 0xD641, 0x4E75};
    std::vector<uint16_t> cave_v = {0x302E, 0x0004, 0x0440, e, 0x0240, 0xFF80, 0x4E75};
    std::vector<uint16_t> cave_anim = {
        0x3601, 0xD668, 0x001C, 0x3003, (uint16_t)(0x5040 | ((k & 7) << 9)), 0xB042, 0x6520, 0x3001,
        0x5E40, 0xB042, 0x6D18, 0x0443, 0x0019, 0xB642, 0x6E10, 0x3002, 0x0640, (uint16_t)(0x14 + k),
        0xB240, 0x6206, 0x023C, 0x00FE, 0x4E75, 0x003C, 0x0001, 0x4E75};
    std::vector<uint16_t> adj = {
        0x0641, e, 0x0440, e, 0xB240, 0x6F0C, 0x3229, 0x0030, 0xD269, 0x0032, 0xE241, 0x3001,
        0x3341, 0x0022, 0x3340, 0x0026, 0x3429, 0x0004, 0xB441, 0x6C02, 0x3401, 0xB440, 0x6F02,
        0x3400, 0x3342, 0x0004, 0x3342, 0x0018};
    std::vector<uint16_t> cave_cam = {0x3029, 0x000E, 0xE948, 0x0440, 0x0140, 0x3340, 0x0026, 0x3340,
                                      0x0032, 0x3229, 0x0022, 0x3341, 0x0030, 0xB2FC, 0xFA80,
                                      (uint16_t)(0x6600 | (adj.size() * 2 + 2))};
    cave_cam.insert(cave_cam.end(), adj.begin(), adj.end());
    cave_cam.push_back(0x4E75);
    const uint16_t sync[] = {0x0C78, 0x0009, 0x9802, 0x672A, 0x2F0E, 0x4DF8, 0xFAC0, 0x4EB9, 0x0000,
                             0xFB10, 0x302E, 0x001E, 0xD16E, 0x0004, 0x302E, 0x001C, 0xD156, 0x3D6E,
                             0x0004, 0x0018, 0x3D56, 0x0014, 0x42AE, 0x001C, 0x2C5F, 0x4E75};
    cave_cam.insert(cave_cam.end(), std::begin(sync), std::end(sync));
    std::vector<RomPatch> p = {
        // sprites: piece culling and the plane-B attached background sprites (Dessert)
        {0x001C3C, {0x0C40, 0x0060}, {0x0C40, lo}},
        {0x001C42, {0x0C40, 0x01E0}, {0x0C40, up}},
        // Dessert background props ($BE/$BF, position x relative to plane B, about 48 px wide): hidden when
        // x leaves (lo-128, hi-128) ($FE8E/$FE94), shown when x is in [-a, len-a] ($FEBA a, $FEC4 len).
        // Up to 2:1 both windows just grow by E (original: hide at -32/352, show in [-24, 344]). Wider, two
        // positions (512 px apart) can be on screen: the shown one is kept where the game draws all its
        // sprites (-96..420) and prop_copy() draws the neighbouring position.
        {0x00FE8E, {0x0C41, 0x0060}, {0x0C41, E > 64 ? (uint16_t)0x0020 : lo}},
        {0x00FE94, {0x0C41, 0x01E0}, {0x0C41, (uint16_t)(E > 64 ? 0x0224 : 0x1E0 + E)}},
        {0x00FEBA, {0x0641, 0x0018}, {0x0641, (uint16_t)(E > 64 ? 0x005F : 0x18 + E)}},
        {0x00FEC4, {0x0642, 0x0170}, {0x0642, (uint16_t)(E > 64 ? 0x0202 : 0x170 + 2 * E)}},
        // objects: delete window, spawner triggers/rows/initial fill
        {0x001B12, {0x0C44, 0x0280, 0x6400, 0x017C, 0x4A44, 0x6B00, 0x0176},
                   {0x3004, 0x0640, e, 0x0C40, (uint16_t)(0x280 + 2 * E), 0x6400, 0x0176}},
        {0x06D53C, {0x302E, 0x0004, 0x362E, 0x0018}, {0x4EB9, 0x0007, (uint16_t)CAVE_H, 0x4E71}},
        {CAVE_H, ff(cave_h.size()), cave_h},
        {0x06D586, {0x302E, 0x0004, 0x0240, 0xFF80}, {0x4EB9, 0x0007, (uint16_t)CAVE_V, 0x4E71}},
        {CAVE_V, ff(cave_v.size()), cave_v},
        // row spawner: 6 cell columns (7 beyond E = 64, where the horizontal spawner reaches one column further)
        {0x06D59C, {0x7E04}, {(uint16_t)(E > 64 ? 0x7E06 : 0x7E05)}},
        {0x06D772, {0x7E00}, {(uint16_t)(0x7E00 | ((-E) & 0xFF))}},
        {0x06D786, {0x0C47, 0x0280}, {0x0C47, (uint16_t)(0x280 + E)}},
        // animated map blocks (Toyland)
        {0x00F338, {0x3601, 0xD668, 0x001C, 0xB642, 0x6500, 0x002C, 0x0642, 0x0014, 0xB242, 0x6222},
                   {0x4EB9, 0x0007, (uint16_t)CAVE_ANIM, 0x6500, 0x002E, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71}},
        {CAVE_ANIM, ff(cave_anim.size()), cave_anim},
        // "object on screen" test used for sound effects
        {0x00200C, {0x2238, 0xFA84, 0xB2A8, 0x0010, 0x6410, 0x0681, 0x0140, 0x0000, 0xB2A8, 0x0010, 0x6504},
                   {0x2228, 0x0010, 0x92B8, 0xFA84, 0x0681, e, 0x0000, 0x0C81, (uint16_t)(320 + 2 * E), 0x0000, 0x6404}},
        // camera clamp E px inside the level (original bounds saved at $FAB0/$FAB2)
        {0x00EEEC, {0x3029, 0x000E, 0xE948}, {0x4EF9, 0x0007, (uint16_t)CAVE_CAM}},
        {CAVE_CAM, ff(cave_cam.size()), cave_cam},
        {0x00F71E, {0x3238, 0xFAA2}, {0x3238, 0xFAB0}},
    };
    // Where the renderer draws a prop's second copy (prop_copy), a prop that leaves its window must take its
    // next position in the same frame - not stay hidden for one frame (bset #2 / rts -> bra $FE3E, the scan)
    if (E > 64) p.push_back({0x00FEAE, {0x08E8, 0x0002, 0x0002, 0x4E75}, {0x6000, 0xFF8E, 0x4E71, 0x4E75}});
    return p;
}

bool wide_gameplay(const MegaDrive& md) {
    uint16_t m = md.ram16(MODE);
    return m == M_GAMEPLAY || m == M_WARP;
}

// The camera X range the original gives the current round: fixed per round (docs/re_coi_wide.md §4),
// min 0 except in the four locked boss arenas, max = map width - 320. Checked against every round.
void round_bounds(const MegaDrive& md, int& lo, int& hi) {
    switch (md.ram16(LEVEL)) {
    case 0x04: lo = 160; break;
    case 0x08: case 0x1B: lo = 192; break;
    case 0x16: lo = 3456; break;
    default: lo = 0; break;
    }
    hi = (int16_t)(uint16_t)((md.ram16(0xFFFA8E) << 4) - 320);
}

// The camera clamp patch adjusts a round's bounds when the round loads. If the widescreen mode
// changes mid-round (menu, or loading a state made in another mode) bring the live range
// ($FAA2/$FAA6) to what this mode wants, and keep the saved originals ($FAB0/$FAB2) right. The
// range only ever narrows as far as the camera allows: a camera jump would drop a streamed column.
// Returns true once the range is the target one.
bool sync_camera_bounds(MegaDrive& md, int E) {
    int lo, hi;
    round_bounds(md, lo, hi);
    if (hi < lo) return true;
    int tmin = lo + E, tmax = hi - E;
    if (tmin > tmax) tmin = tmax = (lo + hi) / 2;
    const int cam = (int16_t)md.ram16(0xFFFA84);
    int mn = (int16_t)md.ram16(0xFFFAA2), mx = (int16_t)md.ram16(0xFFFAA6);
    mn = mn > tmin ? tmin : std::min(tmin, std::max(mn, cam));
    mx = mx < tmax ? tmax : std::max(tmax, std::min(mx, cam));
    md.set_ram16(0xFFFAA2, (uint16_t)mn);
    md.set_ram16(0xFFFAA6, (uint16_t)mx);
    if (E > 0) {
        md.set_ram16(0xFFFAB0, (uint16_t)lo);
        md.set_ram16(0xFFFAB2, (uint16_t)hi);
    }
    return mn == tmin && mx == tmax;
}

// Status bar (window plane, lines 192..223): POWER stays left, TRIES + ITEMS centred, SCORE right;
// gaps repeat the blank columns 8..11; the border rows repeat their 16-px pattern.
std::vector<int16_t> hud_map(int E, bool anchor, bool border) {
    std::vector<int16_t> m(320 + 2 * E);
    auto mod = [](int v, int n) { return ((v % n) + n) % n; };
    for (int x = 0; x < (int)m.size(); x++) {
        int s;
        if (border) s = 64 + mod(x - E, 16);
        else if (!anchor) s = x >= E && x < E + 320 ? x - E : 64 + mod(x - 64, 32);
        else if (x < 64) s = x;
        else if (x < 96 + E) s = 64 + mod(x - 64, 32);
        else if (x < 232 + E) s = x - E;
        else if (x < 240 + 2 * E) s = 64 + mod(x - 232 - E, 32);
        else s = x - 2 * E;
        m[x] = (int16_t)s;
    }
    return m;
}

// Sprite masking (an X=0 sprite) only works after a sprite with X != 0 on the same line, so the game
// arms each group of masking sprites with sprites at X=64, i.e. screen x = -64, which the 2:1 view
// reaches: SAT 0-3 keep objects off the status bar, object $93 places more in Dessert Land. A run of
// X=64 sprites that leads straight to an X=0 one is such an arming group: draw it out of view -
// masking depends only on the raw X, so it keeps working. (Real scenery at X=64 is followed by X=96.)
int sprite_xoff(void* host, int index, uint16_t attr, int x, int y) {
    (void)attr; (void)y;
    if (x != 64) return 0;
    if (index <= 3 && y - 128 >= 192) return -512;   // the status bar's own pair (alone during the Toyland 3 flip)
    const VDP& v = ((const MegaDrive*)host)->vdp;
    const uint32_t sat = (v.reg[5] & ((v.reg[12] & 1) ? 0x7E : 0x7F)) << 9;
    for (int n = 0, i = index; n < 8; n++) {
        i = v.vram[(sat + i * 8 + 3) & 0xFFFF] & 0x7F;
        if (i == 0) return 0;
        int nx = v.vram16(sat + i * 8 + 6) & 0x1FF;
        if (nx == 0) return -512;
        if (nx != 64) return 0;
    }
    return 0;
}

// The status bar is the window plane below line 192 (reg 18 = $98). While the rotating room (Toyland 3)
// redraws, the window briefly covers the whole screen (reg 18 = $80) and still shows the bar there.
bool hud_window(const VDP& v, int line) { return (v.reg[18] & 0x80) && line >= (v.reg[18] & 0x1F) * 8; }

// The 512-px planes hold block columns [X/16-8+k, X/16+23+k] (k = 0/1 by scroll direction, docs §2):
// enough for E <= 64. A wider view reaches further right than that (and the plane wraps onto columns
// the left side still needs), so from block column X/16+24 on the cells are drawn here straight from
// the RAM block map, with the same H/V scroll the VDP used for this line.
void draw_far_columns(const MegaDrive& md, int line, uint8_t* buf, bool plane_b) {
    const VDP& v = md.vdp;
    const uint32_t cam = plane_b ? 0xFFFAC0 : 0xFFFA80;
    const int E = v.ext, TW = 320 + 2 * E;
    const int camx = (int16_t)md.ram16(cam + 4), camy = (int16_t)md.ram16(cam);
    const uint32_t map = 0xFF0000 | (md.ram32(cam + 8) & 0xFFFF), defs = 0xFF0000 | (md.ram32(cam + 0x10) & 0xFFFF);
    const int W = md.ram16(cam + 0x0E), H = md.ram16(cam + 0x0C);
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
    const int py = (line + (v.vsram[plane_b ? 1 : 0] & 0x3FF)) & 0x1FF;
    const int wy = camy + line + (((py - (camy + line)) + 256) & 0x1FF) - 256;
    // Toyland 3's upside-down room ($9801): the column writer ($FA52) walks the map upwards from 25 block
    // rows below its start row (camY/16 - 7) and swaps and V-flips the two cells of each block.
    const bool flip = !plane_b && md.ram8(0xFF9801);
    const int by = flip ? 2 * ((camy >> 4) - 7) + 25 - (wy >> 4) : wy >> 4;
    if (wy < 0 || by < 0 || by >= H) return;
    const int cell_row = flip ? 1 - ((wy >> 3) & 1) : (wy >> 3) & 1;
    const uint16_t vflip = flip ? 0x1000 : 0;
    // Judge what the plane holds by the scroll shown on this line (the VINT wrote it together with the
    // queued columns); camX in RAM may already be the next frame's.
    const int first = ((wx0 >> 4) + 24) * 16;
    int x = std::max(0, first - wx0 + E);
    uint8_t row[8];
    while (x < TW) {
        int wx = wx0 + x - E, bx = wx >> 4;
        int n = std::min(8 - (wx & 7), TW - x);
        if (bx >= 0 && bx < W) {
            uint8_t blk = md.ram8(map + by * W + bx);
            v.pattern_row(md.ram16(defs + blk * 8 + cell_row * 4 + ((wx >> 3) & 1) * 2) ^ vflip, wy & 7, row);
            memcpy(buf + x, row + (wx & 7), n);
        }
        x += n;
    }
}

// Dessert Land's background props ($BE tiles $28A-$29F, $BF tiles $281-$289) repeat every 512 px of plane B
// (position tables $FED8 / $FEEA) but each is a single object showing one position. A view wider than about
// 464 px can hold two: draw the neighbouring position's copy too, if the table has it.
int prop_copy(void* host, int index, uint16_t attr, int x, int y) {
    (void)index; (void)y;
    const MegaDrive& md = *(const MegaDrive*)host;
    const int tile = attr & 0x7FF;
    if ((attr & 0xE000) != 0xE000 || tile < 0x281 || tile > 0x29F) return 0;
    const uint16_t round = md.ram16(LEVEL);
    if (round != 0x15 && round != 0x16) return 0;
    const int E = md.vdp.ext, sx = x - 128;
    const int base = tile >= 0x28A ? 0xE8 : 0x1B8;
    const int k = (int)std::lround((sx + (int16_t)md.ram16(0xFFFAC4) - base) / 512.0);
    if (k + 1 <= 8 && sx + 512 < 320 + E) return 512;
    if (k - 1 >= 0 && sx - 512 + 32 > -E) return -512;
    return 0;
}

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    MegaDrive& md = *(MegaDrive*)host;
    if (!wide_gameplay(md)) return;
    if (md.vdp.ext > 64 && !hud_window(md.vdp, line)) {
        draw_far_columns(md, line, plane_a, false);
        if (!(md.ram8(0xFF9800) & 1)) draw_far_columns(md, line, plane_b, true);   // plane B streamed too
        return;
    }
    if (line < 192 || !hud_window(md.vdp, line)) return;
    static std::vector<int16_t> maps[2][2][128];
    bool anchor = md.vdp.hook_flags & 1;
    bool border = line < 200 || line >= 216;
    std::vector<int16_t>& m = maps[anchor][border][md.vdp.ext & 127];
    if (m.empty()) m = hud_map(md.vdp.ext, anchor, border);
    hud_remap_window_line(md.vdp, line, plane_a, plane_b, m);
}

class CastleOfIllusion : public GameModule {
public:
    const char* id() const override { return "coi"; }
    const char* title() const override { return "Castle of Illusion"; }
    const char* subtitle() const override { return "Starring Mickey Mouse"; }
    const char* year() const override { return "1990"; }
    uint32_t rom_crc32() const override { return 0xBA4E9FD0; }
    uint32_t rom_size() const override { return 0x80000; }
    const char* rom_hint() const override { return "the Castle of Illusion (USA) ROM"; }
    const char* rom_names() const override { return "=MICKEYMOUSE|CASTLEOFILLUSION|ILOVEMICKEYMOUSE"; }
    uint32_t accent_color() const override { return 0xFFB58CFF; }
    int max_ext() const override { return 108; }  // up to 2.39:1 (536 px); beyond 64 the far columns are drawn from the map

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_power")) add(kInfPower);
        if (cfg.cheat("inf_tries")) add(kInfTries);
        if (cfg.cheat("inf_items")) add(kInfItems);
        if (cfg.cheat("inf_continues")) add(kInfContinues);
        if (cfg.cheat("max_power")) add(kMaxPower);
        if (cfg.cheat("debug_select")) add(kDebugSelect);
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, patch_list(cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, patch_list(cfg));
        md.vdp.line_hook = cfg.ext > 0 ? line_hook : nullptr;
        md.vdp.sprite_xoff = cfg.ext > 0 ? sprite_xoff : nullptr;
        md.vdp.sprite_x10 = cfg.ext > 64;   // screen x up to 427 needs VDP X beyond 511
        md.vdp.sprite_copy = cfg.ext > 64 ? prop_copy : nullptr;
        md.vdp.hook_host = &md;
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
        md.module_state[0] = 1;   // re-check the camera range for this mode (see sync_camera_bounds)
    }

    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        // Infinite items also tops the stock up to the maximum (and redraws the HUD counter).
        if (cfg.cheat("inf_items") && md.ram16(MODE) == M_GAMEPLAY && md.ram16(ITEMS) < 30) {
            md.set_ram16(ITEMS, 30);
            md.set_ram32(HUD_ITEMS, ((uint32_t)(0x8010 + 3) << 16) | (0x8010 + 0));
            md.set_ram8(HUD_DIRTY, md.ram8(HUD_DIRTY) | 4);
        }
    }

    void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        info.gameplay = wide_gameplay(md);
        if (md.module_state[0] && md.ram16(MODE) == M_GAMEPLAY && sync_camera_bounds(md, E)) md.module_state[0] = 0;
        if (!info.gameplay || E == 0) return;
        // Mask whatever the (clamped) camera still shows outside the original level bounds -
        // only the narrow boss arenas.
        int camx = (int16_t)md.ram16(0xFFFA84);
        int lo = (int16_t)md.ram16(0xFFFAB0), hi = (int16_t)md.ram16(0xFFFAB2);
        info.mask_left = std::clamp(lo + E - camx, 0, E);
        info.mask_right = std::clamp(camx + E - hi, 0, E);
        if (hud_window(md.vdp, 192)) info.mask_lines = 192;
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, projectiles and spikes can't hurt Mickey. Pits still cost a try."},
            {"inf_power", "Infinite Power", "Hits knock Mickey back but never remove power stars."},
            {"inf_tries", "Infinite Tries", "Losing a life never reduces the TRIES counter."},
            {"inf_items", "Infinite Items", "Apples, marbles and gems never run out."},
            {"inf_continues", "Infinite Continues", "Game over never uses up a continue."},
            {"max_power", "Start With Full Power", "Mickey starts every level with 5 power stars (on Hard: every round and restart)."},
            {"debug_select", "Developer Level Select", "Re-enables the hidden level select on the OPTIONS screen: A steps through the levels, START plays it. (B works too, but also changes the highlighted option.)"},
        };
    }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Level& l : kLevels) v.push_back({l.group, l.name});
        return v;
    }

    bool start_level(MegaDrive& md, const GameConfig& cfg, int entry, int) override {
        if (entry < 0 || entry >= (int)(sizeof(kLevels) / sizeof(kLevels[0]))) return false;
        int index = kLevels[entry].index;
        md.power_on();
        configure(md, cfg);
        auto mode = [&]() { return md.ram16(MODE); };
        auto idle = [&]() { return md.ram8(PAD_HELD) == 0; };
        // power-on -> opening story -> title -> OPTIONS (docs section 2.4)
        if (!run_until(md, [&]() { return mode() == 0x06; }, 2000)) return false;
        if (!press_until(md, MD_START, [&]() { return mode() == 0x12; }, idle)) return false;
        if (!press_until(md, MD_START, [&]() { return mode() != 0x12; }, idle)) return false;
        if (!run_until(md, [&]() { return mode() == M_OPTIONS; }, 300)) return false;
        md.run_frame();
        md.run_frame();
        md.set_ram16(LEVEL, (uint16_t)index);
        // The game's own OPTIONS START handler derives round/act, continues and difficulty.
        if (!press_until(md, MD_START, [&]() { return mode() == M_LEVEL_INIT || mode() == 0x28; }, idle)) return false;
        md.set_ram16(MODE, M_LEVEL_INIT);   // skip the castle hallway / story cut-scenes
        return run_until(md, [&]() { return mode() == M_GAMEPLAY && md.ram16(LEVEL) == index; }, 4000);
    }

    std::vector<SoundEntry> sounds() const override {
        return std::vector<SoundEntry>(std::begin(kSounds), std::end(kSounds));
    }

    void jukebox_boot(MegaDrive& md) override {
        md.power_on();
        // The driver is uploaded at boot and silenced by mode 0; park the game on the SEGA logo.
        run_until(md, [&]() { return md.ram16(MODE) == 0x02; }, 600);
        for (int i = 0; i < 10; i++) md.run_frame();
        freeze_68k(md);
        stop_sound(md);
        for (int i = 0; i < 4; i++) md.run_frame();   // let the driver take the stop request
    }

    void play_sound(MegaDrive& md, int id) override {
        if (id < 0xA0) md.zram[Z_MUSIC] = (uint8_t)id;
        else md.zram[Z_SFX2] = (uint8_t)id;   // spare queue slot the game never uses
    }

    void stop_sound(MegaDrive& md) override { md.zram[Z_SFX] = 0xD1; }
};

}  // namespace

GameModule* create_coi_module() { return new CastleOfIllusion(); }
