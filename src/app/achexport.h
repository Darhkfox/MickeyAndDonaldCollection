// Achievements exported for other tools: achievements.csv in the columns PlayniteAchievements' spreadsheet import
// reads (ID, Title, Description, Points, Trophy Type, Hidden, Rarity, Category, Progress, Progress Total, Unlocked,
// Unlock Time, Unlocked Icon, Locked Icon) and an icons folder with each achievement's badge, earned and locked (PNG,
// drawn here: the port's own pictures, a colour per game and a symbol per achievement).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../game/game.h"

class AchievementStore;

// One game's achievements (or the collection's), under the name the CSV's Category column gives them.
struct AchievementGroup {
    std::string title;   // "Castle of Illusion", ... "Mickey & Donald Collection"
    std::vector<AchievementDef> defs;
};

// Writes dir/achievements.csv and dir/icons/*.png; false, with why, if it couldn't.
bool export_achievements(const std::wstring& dir, const std::vector<AchievementGroup>& groups, const AchievementStore& store,
                         std::string* err);

// An achievement's badge, size x size pixels (0x00RRGGBB).
void draw_achievement_icon(const char* id, bool locked, int size, std::vector<uint32_t>& px);

// Its tier (0 bronze, 1 silver, 2 gold, 3 platinum) and points (10 / 25 / 50 / 100).
int achievement_tier(const char* id);
int achievement_points(const char* id);
