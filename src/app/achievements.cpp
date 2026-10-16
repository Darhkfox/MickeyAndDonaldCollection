#include "achievements.h"
#include "sys.h"
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string today() {
    time_t t = time(nullptr);
    tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return buf;
}
}  // namespace

const char* const kGrandTourGames[3] = {"coi_minnie", "qs_treasure", "woi_magician"};

std::vector<AchievementDef> collection_achievements() {
    return {
        {"all_grandtour", "Grand Tour", "Finish all three games.", false, 3},
        {"all_complete", "Completionist", "Unlock every other achievement.", false, 0},
    };
}

void AchievementStore::load(const std::wstring& path) {
    path_ = path;
    unlocked_.clear();
    progress_.clear();
    std::string text;
    if (!read_text(path, text)) return;
    std::istringstream f(text);
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') { section = trim(line.substr(1, line.find(']') - 1)); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (section == "unlocked") unlocked_[k] = v;
        else if (section == "progress") progress_[k] = atoi(v.c_str());
        else if (section == "general" && k == "notice_shown") notice_shown = atoi(v.c_str()) != 0;
    }
}

bool AchievementStore::save() const {
    if (path_.empty()) return false;
    std::ostringstream o;
    o << "; Mickey & Donald Collection - achievements earned (delete this file to start again)\n\n[unlocked]\n";
    for (const auto& kv : unlocked_) o << kv.first << "=" << kv.second << "\n";
    o << "\n[progress]\n";
    for (const auto& kv : progress_) o << kv.first << "=" << kv.second << "\n";
    o << "\n[general]\nnotice_shown=" << (notice_shown ? 1 : 0) << "\n";
    const std::string s = o.str();
    return write_file(path_, s.data(), s.size());
}

std::string AchievementStore::unlocked_on(const std::string& id) const {
    auto it = unlocked_.find(id);
    return it == unlocked_.end() ? std::string() : it->second;
}

bool AchievementStore::unlock(const std::string& id) {
    if (unlocked(id)) return false;
    unlocked_[id] = today();
    return true;
}

int AchievementStore::progress(const std::string& id) const {
    auto it = progress_.find(id);
    return it == progress_.end() ? 0 : it->second;
}

bool AchievementStore::set_progress(const std::string& id, int value) {
    int& p = progress_[id];
    if (value <= p) return false;
    p = value;
    return true;
}
