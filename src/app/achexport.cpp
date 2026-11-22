// Achievements exported for other tools: see achexport.h. (The badge drawing follows the Super Mario All-Stars port's.)
#include "achexport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "achievements.h"
#include "png.h"
#include "sys.h"

namespace {

// ---- what each badge shows ------------------------------------------------------------------------------------

enum Glyph {
    kStar, kHeart, kTrophy, kTrophyStar, kClock, kFire, kBurst, kHammer, kArrowL, kArrowR, kMoon, kLeaf, kWing, kCard,
    kChest, kDoor, kMask, kBomb, kShield, kBlock, kWand, kBoot, kCloud, kDrop, kCrown,
    kApple, kCup, kCake, kKey, kPlunger, kMountain, kText
};

struct Look {
    const char* id;
    Glyph glyph;
    const char* text;   // (kText)
    int tier;           // 0 bronze, 1 silver, 2 gold, 3 platinum
};

const Look kLooks[] = {
    {"coi_timber", kLeaf, nullptr, 0},        {"coi_toyland", kBlock, nullptr, 0},
    {"coi_storm", kCloud, nullptr, 1},        {"coi_desserts", kCake, nullptr, 1},
    {"coi_jester", kClock, nullptr, 1},       {"coi_minnie", kHeart, nullptr, 2},
    {"coi_hard", kText, "HARD", 2},           {"coi_noscratch", kStar, nullptr, 1},
    {"coi_flawless", kShield, nullptr, 2},    {"coi_iron", kText, "IRON", 2},
    {"coi_tea", kCup, nullptr, 0},            {"coi_double", kText, "X2", 0},
    {"coi_hattrick", kText, "X3", 1},         {"coi_apple", kApple, nullptr, 0},
    {"coi_juggler", kText, "30", 1},          {"coi_score", kText, "100K", 1},
    {"coi_speedy", kBoot, nullptr, 2},

    {"qs_herokey", kKey, nullptr, 0},         {"qs_gyro", kText, "GUM", 0},
    {"qs_dracula", kMoon, nullptr, 1},        {"qs_tiger", kBurst, nullptr, 1},
    {"qs_ghosts", kMask, nullptr, 1},         {"qs_pete", kHammer, nullptr, 1},
    {"qs_treasure", kChest, nullptr, 2},      {"qs_steel", kShield, nullptr, 1},
    {"qs_untouchable", kHeart, nullptr, 2},   {"qs_minecart", kArrowR, nullptr, 1},
    {"qs_quack", kFire, nullptr, 0},          {"qs_rampage", kText, "X5", 1},
    {"qs_climber", kPlunger, nullptr, 1},     {"qs_bird", kWing, nullptr, 1},
    {"qs_gum", kBomb, nullptr, 0},            {"qs_popcorn", kText, "POP", 0},
    {"qs_flyer", kText, "15", 1},             {"qs_score", kText, "1M", 2},

    {"woi_forest", kLeaf, nullptr, 0},        {"woi_mountains", kMountain, nullptr, 0},
    {"woi_sea", kDrop, nullptr, 0},           {"woi_house", kDoor, nullptr, 1},
    {"woi_magician", kCrown, nullptr, 2},     {"woi_mickey", kText, "M", 2},
    {"woi_donald", kText, "D", 2},            {"woi_together", kText, "2P", 2},
    {"woi_spotless", kShield, nullptr, 2},    {"woi_carpet", kCloud, nullptr, 1},
    {"woi_deck", kCard, nullptr, 1},          {"woi_hearts", kHeart, nullptr, 1},
    {"woi_cape", kText, "X3", 1},             {"woi_magic", kWand, nullptr, 0},
    {"woi_hand", kText, "UP", 0},             {"woi_detour", kArrowL, nullptr, 0},

    {"all_grandtour", kTrophyStar, nullptr, 2}, {"all_complete", kTrophy, nullptr, 3},
};

const Look& look_of(const char* id) {
    static const Look kDefault = {"", kStar, nullptr, 0};
    for (const Look& l : kLooks)
        if (std::strcmp(l.id, id) == 0) return l;
    return kDefault;
}

// ---- drawing: shapes in a unit square (y down), sampled 4x4 per pixel ---------------------------------------------

// A 5x7 font for the badges' words (rows top to bottom, bit 4 the left column).
struct FontChar { char c; uint8_t rows[7]; };
const FontChar kFont[] = {
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},     {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},    {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},   {'7', {31, 1, 2, 4, 8, 8, 8}},      {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},   {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 17, 25, 21, 19, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}}, {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},  {'-', {0, 0, 0, 31, 0, 0, 0}},
};

// Inside the text `s`, centred at (cx, cy), `h` tall (narrowed to `max_w` wide)?
bool in_text(const char* s, float cx, float cy, float h, float max_w, float x, float y) {
    const int n = (int)std::strlen(s);
    float cell = h / 7;
    float w = (6 * n - 1) * cell;
    if (w > max_w) { cell *= max_w / w; w = max_w; }
    const float x0 = cx - w / 2, y0 = cy - 7 * cell / 2;
    const int col = (int)std::floor((x - x0) / cell), row = (int)std::floor((y - y0) / cell);
    if (col < 0 || row < 0 || row >= 7 || col >= 6 * n - 1) return false;
    const int ch = col / 6, bit = col % 6;
    if (bit == 5) return false;
    for (const FontChar& f : kFont)
        if (f.c == s[ch]) return (f.rows[row] >> (4 - bit)) & 1;
    return false;
}

struct P { float x, y; };

bool circ(P p, float cx, float cy, float r) { return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r; }
bool ell(P p, float cx, float cy, float rx, float ry) {
    const float a = (p.x - cx) / rx, b = (p.y - cy) / ry;
    return a * a + b * b <= 1;
}
bool rect(P p, float x0, float y0, float x1, float y1) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
bool rrect(P p, float x0, float y0, float x1, float y1, float r) {
    if (!rect(p, x0, y0, x1, y1)) return false;
    const float cx = std::clamp(p.x, x0 + r, x1 - r), cy = std::clamp(p.y, y0 + r, y1 - r);
    return circ(p, cx, cy, r);
}
bool ring(P p, float cx, float cy, float r0, float r1) { return circ(p, cx, cy, r1) && !circ(p, cx, cy, r0); }
bool seg(P p, float x0, float y0, float x1, float y1, float w) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float t = std::clamp(((p.x - x0) * dx + (p.y - y0) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    return circ(p, x0 + t * dx, y0 + t * dy, w);
}
bool poly(P p, std::initializer_list<P> pts) {
    bool in = false;
    const P* v = pts.begin();
    const size_t n = pts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        if ((v[i].y > p.y) != (v[j].y > p.y) && p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x)
            in = !in;
    return in;
}
bool star_at(P p, float cx, float cy, float R, float r, int points) {
    const float a = std::atan2(p.y - cy, p.x - cx) + 3.14159265f / 2;
    const float d = std::sqrt((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy));
    const float seg = 2 * 3.14159265f / points;
    float t = std::fmod(a + 2 * 3.14159265f * 4, seg) / seg;   // 0 at a point, 0.5 between two
    t = std::abs(t - 0.5f) * 2;                                // 1 at a point, 0 between
    const float edge = r + (R - r) * t * t * (3 - 2 * t) * 0.92f + (R - r) * 0.08f * t;
    return d <= edge;
}

bool glyph_at(const Look& l, P p) {
    switch (l.glyph) {
    case kStar: return star_at(p, 0.5f, 0.47f, 0.30f, 0.13f, 5);
    case kBurst: return star_at(p, 0.5f, 0.45f, 0.29f, 0.15f, 8) && !circ(p, 0.5f, 0.45f, 0.07f);
    case kHeart:
        return circ(p, 0.395f, 0.37f, 0.125f) || circ(p, 0.605f, 0.37f, 0.125f) ||
               poly(p, {{0.28f, 0.41f}, {0.72f, 0.41f}, {0.5f, 0.68f}});
    case kTrophy:
    case kTrophyStar: {
        const bool cup = (rect(p, 0.33f, 0.20f, 0.67f, 0.38f) || (ell(p, 0.5f, 0.38f, 0.17f, 0.15f) && p.y >= 0.38f));
        const bool handles = (ring(p, 0.33f, 0.30f, 0.045f, 0.085f) && p.x < 0.33f) ||
                             (ring(p, 0.67f, 0.30f, 0.045f, 0.085f) && p.x > 0.67f);
        const bool foot = rect(p, 0.465f, 0.50f, 0.535f, 0.61f) || rrect(p, 0.36f, 0.60f, 0.64f, 0.69f, 0.02f);
        const bool star = l.glyph == kTrophyStar && star_at(p, 0.5f, 0.33f, 0.10f, 0.045f, 5);
        return (cup && !star) || handles || foot;
    }
    case kCrown:
        return (poly(p, {{0.25f, 0.62f}, {0.25f, 0.30f}, {0.375f, 0.47f}, {0.5f, 0.25f}, {0.625f, 0.47f}, {0.75f, 0.30f},
                         {0.75f, 0.62f}}) ||
                rect(p, 0.25f, 0.62f, 0.75f, 0.69f) || circ(p, 0.25f, 0.29f, 0.04f) || circ(p, 0.5f, 0.24f, 0.04f) ||
                circ(p, 0.75f, 0.29f, 0.04f)) &&
               !circ(p, 0.5f, 0.53f, 0.04f);
    case kClock:
        return ring(p, 0.5f, 0.47f, 0.19f, 0.245f) || seg(p, 0.5f, 0.47f, 0.5f, 0.32f, 0.025f) ||
               seg(p, 0.5f, 0.47f, 0.61f, 0.53f, 0.025f) || rrect(p, 0.45f, 0.15f, 0.55f, 0.21f, 0.015f);
    case kFire:   // (a tall flame with a tongue each side)
        return (circ(p, 0.5f, 0.53f, 0.165f) || poly(p, {{0.337f, 0.50f}, {0.47f, 0.17f}, {0.66f, 0.47f}}) ||
                circ(p, 0.33f, 0.60f, 0.085f) || poly(p, {{0.25f, 0.60f}, {0.27f, 0.36f}, {0.40f, 0.56f}}) ||
                circ(p, 0.68f, 0.60f, 0.08f) || poly(p, {{0.61f, 0.58f}, {0.75f, 0.40f}, {0.76f, 0.60f}})) &&
               !(circ(p, 0.5f, 0.58f, 0.075f) || poly(p, {{0.428f, 0.57f}, {0.49f, 0.39f}, {0.572f, 0.57f}}));
    case kHammer: return rrect(p, 0.28f, 0.22f, 0.72f, 0.38f, 0.03f) || rect(p, 0.46f, 0.38f, 0.54f, 0.71f);
    case kArrowR:
        return poly(p, {{0.24f, 0.39f}, {0.50f, 0.39f}, {0.50f, 0.26f}, {0.77f, 0.46f}, {0.50f, 0.66f}, {0.50f, 0.53f},
                        {0.24f, 0.53f}});
    case kArrowL:
        return poly(p, {{0.76f, 0.39f}, {0.50f, 0.39f}, {0.50f, 0.26f}, {0.23f, 0.46f}, {0.50f, 0.66f}, {0.50f, 0.53f},
                        {0.76f, 0.53f}});
    case kMoon: return circ(p, 0.48f, 0.46f, 0.23f) && !circ(p, 0.58f, 0.39f, 0.20f);
    case kLeaf:
        return (circ(p, 0.36f, 0.48f, 0.25f) && circ(p, 0.64f, 0.42f, 0.25f) && !seg(p, 0.37f, 0.63f, 0.63f, 0.27f, 0.012f)) ||
               seg(p, 0.30f, 0.71f, 0.40f, 0.60f, 0.02f);
    case kWing:
        return ell(p, 0.42f, 0.30f, 0.24f, 0.075f) || ell(p, 0.45f, 0.43f, 0.21f, 0.07f) ||
               ell(p, 0.48f, 0.56f, 0.17f, 0.065f) || ell(p, 0.66f, 0.45f, 0.08f, 0.22f);
    case kCard:
        return rrect(p, 0.32f, 0.20f, 0.68f, 0.70f, 0.04f) && !star_at(p, 0.5f, 0.46f, 0.12f, 0.05f, 5) &&
               !(rrect(p, 0.35f, 0.23f, 0.65f, 0.67f, 0.03f) && !rrect(p, 0.36f, 0.24f, 0.64f, 0.66f, 0.025f));
    case kChest:
        return ((rrect(p, 0.27f, 0.37f, 0.73f, 0.68f, 0.03f) || (ell(p, 0.5f, 0.37f, 0.23f, 0.13f) && p.y <= 0.37f)) &&
                !rect(p, 0.27f, 0.40f, 0.73f, 0.43f)) &&
               !rrect(p, 0.465f, 0.44f, 0.535f, 0.52f, 0.01f);
    case kDoor:
        return (rect(p, 0.33f, 0.34f, 0.67f, 0.70f) || (circ(p, 0.5f, 0.34f, 0.17f) && p.y <= 0.34f)) &&
               !circ(p, 0.60f, 0.52f, 0.025f);
    case kMask:   // (a ghost: a round head over a wavy hem, two eyes)
        return ((circ(p, 0.5f, 0.40f, 0.21f) || rect(p, 0.29f, 0.40f, 0.71f, 0.62f) || circ(p, 0.34f, 0.63f, 0.05f) ||
                 circ(p, 0.5f, 0.63f, 0.05f) || circ(p, 0.66f, 0.63f, 0.05f)) &&
                !ell(p, 0.42f, 0.40f, 0.04f, 0.06f) && !ell(p, 0.58f, 0.40f, 0.04f, 0.06f));
    case kBomb:
        return (circ(p, 0.47f, 0.51f, 0.19f) || rect(p, 0.53f, 0.27f, 0.62f, 0.34f) ||
                seg(p, 0.58f, 0.27f, 0.66f, 0.19f, 0.015f) || star_at(p, 0.69f, 0.17f, 0.06f, 0.025f, 5)) &&
               !circ(p, 0.40f, 0.45f, 0.04f);
    case kShield:
        return poly(p, {{0.27f, 0.22f}, {0.73f, 0.22f}, {0.73f, 0.44f}, {0.5f, 0.71f}, {0.27f, 0.44f}}) &&
               !(seg(p, 0.38f, 0.44f, 0.47f, 0.53f, 0.03f) || seg(p, 0.47f, 0.53f, 0.63f, 0.35f, 0.03f));
    case kBlock:   // (a toy block with a letter-like notch)
        return rrect(p, 0.27f, 0.23f, 0.73f, 0.69f, 0.04f) && !ring(p, 0.5f, 0.46f, 0.09f, 0.13f) &&
               !(rrect(p, 0.31f, 0.27f, 0.69f, 0.65f, 0.03f) && !rrect(p, 0.32f, 0.28f, 0.68f, 0.64f, 0.025f));
    case kWand: return seg(p, 0.30f, 0.70f, 0.55f, 0.40f, 0.03f) || star_at(p, 0.60f, 0.32f, 0.15f, 0.065f, 5);
    case kBoot:
        return poly(p, {{0.34f, 0.20f}, {0.56f, 0.20f}, {0.56f, 0.50f}, {0.74f, 0.56f}, {0.74f, 0.68f}, {0.30f, 0.68f},
                        {0.30f, 0.50f}}) &&
               !rect(p, 0.34f, 0.27f, 0.56f, 0.30f);
    case kCloud:
        return circ(p, 0.37f, 0.50f, 0.12f) || circ(p, 0.52f, 0.41f, 0.16f) || circ(p, 0.66f, 0.50f, 0.11f) ||
               rrect(p, 0.30f, 0.48f, 0.72f, 0.62f, 0.06f);
    case kDrop:
        return (circ(p, 0.5f, 0.53f, 0.17f) || poly(p, {{0.335f, 0.50f}, {0.5f, 0.18f}, {0.665f, 0.50f}})) &&
               !ell(p, 0.44f, 0.54f, 0.035f, 0.07f);
    case kApple:
        return ((circ(p, 0.43f, 0.51f, 0.18f) || circ(p, 0.57f, 0.51f, 0.18f)) && !circ(p, 0.5f, 0.32f, 0.045f) &&
                !ell(p, 0.42f, 0.45f, 0.035f, 0.06f)) ||
               seg(p, 0.5f, 0.35f, 0.53f, 0.22f, 0.018f) || ell(p, 0.61f, 0.25f, 0.075f, 0.035f);
    case kCup:   // (a teacup on its saucer)
        return (ell(p, 0.46f, 0.42f, 0.20f, 0.20f) && p.y >= 0.40f) || rect(p, 0.25f, 0.38f, 0.67f, 0.42f) ||
               (ring(p, 0.68f, 0.48f, 0.04f, 0.075f) && p.x > 0.64f) || ell(p, 0.46f, 0.66f, 0.28f, 0.045f) ||
               seg(p, 0.40f, 0.30f, 0.43f, 0.20f, 0.012f) || seg(p, 0.52f, 0.30f, 0.49f, 0.20f, 0.012f);
    case kCake:
        return ((rrect(p, 0.25f, 0.48f, 0.75f, 0.69f, 0.03f) || rrect(p, 0.34f, 0.35f, 0.66f, 0.48f, 0.03f)) &&
                !rect(p, 0.25f, 0.56f, 0.75f, 0.585f) && !rect(p, 0.34f, 0.415f, 0.66f, 0.435f)) ||
               rect(p, 0.48f, 0.23f, 0.52f, 0.35f) || ell(p, 0.5f, 0.19f, 0.03f, 0.045f);
    case kKey:
        return ring(p, 0.34f, 0.46f, 0.06f, 0.13f) || rect(p, 0.46f, 0.43f, 0.77f, 0.49f) ||
               rect(p, 0.64f, 0.49f, 0.68f, 0.58f) || rect(p, 0.72f, 0.49f, 0.77f, 0.60f);
    case kPlunger:
        return seg(p, 0.5f, 0.20f, 0.5f, 0.50f, 0.03f) || circ(p, 0.5f, 0.19f, 0.05f) ||
               (poly(p, {{0.38f, 0.49f}, {0.62f, 0.49f}, {0.72f, 0.67f}, {0.28f, 0.67f}}) && !rect(p, 0.28f, 0.55f, 0.72f, 0.57f));
    case kMountain:
        return poly(p, {{0.17f, 0.68f}, {0.42f, 0.26f}, {0.56f, 0.48f}, {0.65f, 0.36f}, {0.84f, 0.68f}});
    case kText: return in_text(l.text, 0.5f, 0.45f, 0.30f, 0.62f, p.x, p.y);
    }
    return false;
}

struct Col { float r, g, b; };
Col mix(Col a, Col b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Col hex(uint32_t v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
uint32_t rgb(float r, float g, float b) {
    auto c = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255); };
    return c(r) << 16 | c(g) << 8 | c(b);
}

// The game an achievement belongs to, by its id's prefix: its colour and its short name along the badge's foot.
Col game_colour(const char* id) {
    if (!std::strncmp(id, "coi_", 4)) return hex(0x7B4FC8);   // Castle of Illusion: purple
    if (!std::strncmp(id, "qs_", 3)) return hex(0xD8741A);    // QuackShot: orange
    if (!std::strncmp(id, "woi_", 4)) return hex(0x2479C2);   // World of Illusion: blue
    return hex(0xB8862A);                                     // the collection: gold
}
const char* game_tag(const char* id) {
    if (!std::strncmp(id, "coi_", 4)) return "COI";
    if (!std::strncmp(id, "qs_", 3)) return "QS";
    if (!std::strncmp(id, "woi_", 4)) return "WOI";
    return "ALL";
}

// One sample: the badge's colour at (x, y).
Col sample_badge(const char* id, const Look& l, float x, float y) {
    static const uint32_t kTier[4] = {0xC27A3A, 0xC9D1DB, 0xF3C431, 0x9FE3F0};
    const Col back = hex(0x0F1117);
    const P p = {x, y};
    if (!rrect(p, 0.0f, 0.0f, 1.0f, 1.0f, 0.17f)) return back;
    // the tier's rim, lit from above
    if (!rrect(p, 0.045f, 0.045f, 0.955f, 0.955f, 0.13f)) {
        const Col t = hex(kTier[std::clamp(l.tier, 0, 3)]);
        return mix(mix(t, hex(0xFFFFFF), 0.35f), mix(t, back, 0.35f), y);
    }
    // the game's colour, lighter towards the top left
    const Col g = game_colour(id);
    const float light = std::clamp(1.0f - (x * 0.4f + y * 0.9f), 0.0f, 1.0f);
    Col c = mix(mix(g, back, 0.35f), mix(g, hex(0xFFFFFF), 0.18f), light);
    // the game's name along the bottom
    if (y > 0.775f && y < 0.925f) {
        c = mix(c, back, 0.45f);
        if (in_text(game_tag(id), 0.5f, 0.852f, 0.085f, 0.6f, x, y)) c = hex(0xF2F2F2);
        return c;
    }
    // the symbol, white over a soft shadow
    if (glyph_at(l, {x - 0.022f, y - 0.03f})) c = mix(c, back, 0.55f);
    if (glyph_at(l, p)) c = mix(hex(0xFFFFFF), hex(0xFFE9B8), y);
    return c;
}

void csv_field(std::string& out, const std::string& v) {
    if (v.find_first_of(",\"\r\n") == std::string::npos) { out += v; return; }
    out += '"';
    for (char c : v) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
}

}  // namespace

int achievement_tier(const char* id) { return look_of(id).tier; }

int achievement_points(const char* id) {
    static const int kPoints[4] = {10, 25, 50, 100};
    return kPoints[std::clamp(look_of(id).tier, 0, 3)];
}

void draw_achievement_icon(const char* id, bool locked, int size, std::vector<uint32_t>& px) {
    const Look& l = look_of(id);
    px.assign((size_t)size * size, 0);
    constexpr int kSub = 4;
    for (int py = 0; py < size; py++)
        for (int pxx = 0; pxx < size; pxx++) {
            float r = 0, g = 0, b = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++) {
                    const float x = (pxx + (sx + 0.5f) / kSub) / size, y = (py + (sy + 0.5f) / kSub) / size;
                    const Col c = sample_badge(id, l, x, y);
                    r += c.r; g += c.g; b += c.b;
                }
            const float n = kSub * kSub;
            r /= n; g /= n; b /= n;
            if (locked) {   // (grey and dimmed, a little cool)
                const float v = (0.3f * r + 0.59f * g + 0.11f * b) * 0.55f;
                r = v * 0.95f; g = v; b = v * 1.08f;
            }
            px[(size_t)py * size + pxx] = rgb(r, g, b);
        }
}

bool export_achievements(const std::wstring& dir, const std::vector<AchievementGroup>& groups, const AchievementStore& store,
                         std::string* err) {
    const std::wstring icons = path_join(dir, L"icons");
    make_dir(dir);
    make_dir(icons);
    constexpr int kSize = 128;
    static const char* kTrophy[4] = {"bronze", "silver", "gold", "platinum"};
    std::string csv = "\xEF\xBB\xBF";   // (UTF-8, marked)
    csv += "ID,Title,Description,Points,Trophy Type,Hidden,Rarity,Category,Progress,Progress Total,Unlocked,Unlock Time,"
           "Unlocked Icon,Locked Icon\r\n";
    std::vector<uint32_t> px;
    for (const AchievementGroup& grp : groups)
        for (const AchievementDef& d : grp.defs) {
            for (int locked = 0; locked < 2; locked++) {
                draw_achievement_icon(d.id, locked != 0, kSize, px);
                const std::wstring file = path_join(icons, from_utf8(std::string(d.id) + (locked ? "_locked.png" : ".png")));
                if (!write_png(file, px.data(), kSize, kSize)) {
                    if (err) *err = "couldn't write " + to_utf8(file);
                    return false;
                }
            }
            const bool have = store.unlocked(d.id);
            std::string when = store.unlocked_on(d.id);
            if (when.size() == 10) when += " 00:00:00";   // (the day only is kept)
            std::string progress, total;
            if (d.target > 0) {
                int v = store.progress(d.id);
                if (d.set) {   // a set of bits: how many were found
                    int n = 0;
                    for (int b = 0; b < 31; b++) n += (v >> b) & 1;
                    v = n;
                }
                if (have) v = d.target;
                progress = std::to_string(std::min(v, d.target));
                total = std::to_string(d.target);
            }
            const std::string fields[14] = {d.id, d.name, d.desc, std::to_string(achievement_points(d.id)),
                                            kTrophy[achievement_tier(d.id)], "False", "", grp.title, progress, total,
                                            have ? "True" : "False", have ? when : "",
                                            std::string("icons/") + d.id + ".png", std::string("icons/") + d.id + "_locked.png"};
            for (int i = 0; i < 14; i++) {
                if (i) csv += ',';
                csv_field(csv, fields[i]);
            }
            csv += "\r\n";
        }
    if (!write_file(path_join(dir, L"achievements.csv"), csv.data(), csv.size())) {
        if (err) *err = "couldn't write achievements.csv";
        return false;
    }
    return true;
}
