// Achievements: what has been earned (achievements.ini in the data folder) and the collection-wide ones.
// Each game module lists its own achievements and checks them every frame (GameModule::achievement_frame).
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../game/game.h"

class AchievementStore {
public:
    void load(const std::wstring& path);
    bool save() const;

    bool unlocked(const std::string& id) const { return unlocked_.count(id) != 0; }
    std::string unlocked_on(const std::string& id) const;   // "2026-10-16", or "" if not earned
    bool unlock(const std::string& id);                     // true if it was newly earned
    int  progress(const std::string& id) const;
    bool set_progress(const std::string& id, int value);    // keeps the highest; true if it grew

    bool notice_shown = false;   // the one-time "achievements are on" explanation was shown

private:
    std::wstring path_;
    std::map<std::string, std::string> unlocked_;   // id -> date earned
    std::map<std::string, int> progress_;
};

// Achievements across the collection (not tied to one game's RAM): the app checks them after every unlock.
std::vector<AchievementDef> collection_achievements();
extern const char* const kGrandTourGames[3];   // the three "finished the game" achievements Grand Tour needs
