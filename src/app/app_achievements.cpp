// Achievements in the app: the run being played, unlocks and their pop-up, the Achievements pages, the setting.
// What each achievement means is the game module's (GameModule::achievements / achievement_frame).
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

void App::ach_startup() {
    ach_.load(path_join(data_dir(), L"achievements.ini"));
    if (autotest_ || ach_.notice_shown) return;
    ach_.notice_shown = true;
    ach_.save();
    if (!settings_.achievements) return;
    // Once: what achievements change (rewind and fast forward were simply there in version 1.0).
    MenuPage p;
    p.title = "New: Achievements";
    p.subtitle = "Earn them in all three games: bosses, worlds, skill and a few secrets.";
    p.wide = true;
    p.items.push_back(MenuItem::info("While achievements are on, cheats, rewind and fast forward are off."));
    p.items.push_back(MenuItem::info("Switch them off under Options to use those instead (no achievements then)."));
    p.items.push_back(MenuItem::action("Got it", [this]() { menu_.close(); }));
    p.items.push_back(MenuItem::action("Switch achievements off", [this]() {
        set_achievements(false);
        save_settings();
        menu_.close();
    }, "Cheats, rewind and fast forward stay on; you can switch achievements on again under Options."));
    menu_.open(std::move(p));
}

void App::ach_new_run(bool counts) {
    ach_ctx_ = AchievementCtx();
    ach_ctx_.run_counts = counts;
    // the next frame is compared with the machine as it is now (after power-on or the level select's set-up)
    if (session_) ach_ctx_.prev.assign(session_->md().ram, session_->md().ram + sizeof(session_->md().ram));
}

void App::set_achievements(bool on) {
    if (on == settings_.achievements) return;
    settings_.achievements = on;
    // A playthrough only counts if achievements were on all along; switching on mid-level starts the checks afresh.
    ach_ctx_.run_counts = false;
    ach_ctx_.reset_scratch();
    if (on) rewind_.clear();
    apply_settings();   // cheats leave (or come back into) the running game
}

void App::ach_frame() {
    GameModule* m = session_->module();
    const MegaDrive& md = session_->md();
    m->achievement_frame(md, ach_ctx_);
    ach_ctx_.prev.assign(md.ram, md.ram + sizeof(md.ram));
    AchievementCtx& c = ach_ctx_;
    if (c.unlocked.empty() && c.progress.empty() && c.counted.empty() && c.marked.empty()) return;
    const std::vector<AchievementDef> defs = m->achievements();
    auto ok = [&](int i) { return i >= 0 && i < (int)defs.size(); };
    bool dirty = false;
    for (const auto& pr : c.progress)
        if (ok(pr.first)) dirty = ach_.set_progress(defs[pr.first].id, pr.second) || dirty;
    for (int i : c.counted)      // a count kept across sessions (e.g. plane trips)
        if (ok(i)) {
            const int n = ach_.progress(defs[i].id) + 1;
            dirty = ach_.set_progress(defs[i].id, n) || dirty;
            if (defs[i].target > 0 && n >= defs[i].target) c.unlocked.push_back(i);
        }
    for (const auto& pr : c.marked)   // one of a set of things found (e.g. tea cups)
        if (ok(pr.first) && pr.second >= 0 && pr.second < 31) {
            const int mask = ach_.progress(defs[pr.first].id) | (1 << pr.second);
            dirty = ach_.set_progress(defs[pr.first].id, mask) || dirty;
            int found = 0;
            for (int b = 0; b < 31; b++) found += (mask >> b) & 1;
            if (found >= defs[pr.first].target) c.unlocked.push_back(pr.first);
        }
    for (int i : c.unlocked)
        if (ok(i)) ach_award(defs[i].id, defs[i].name);
    c.unlocked.clear();
    c.progress.clear();
    c.counted.clear();
    c.marked.clear();
    if (dirty) ach_.save();
}

void App::ach_award(const std::string& id, const std::string& name) {
    if (!ach_.unlock(id)) return;
    ach_banners_.push_back({name, 0.0f});
    ach_check_collection();
    ach_.save();
}

// Grand Tour (finish all three games) and Completionist (everything else).
void App::ach_check_collection() {
    int finished = 0;
    for (const char* id : kGrandTourGames) finished += ach_.unlocked(id) ? 1 : 0;
    ach_.set_progress("all_grandtour", finished);
    if (finished == 3 && ach_.unlock("all_grandtour")) ach_banners_.push_back({"Grand Tour", 0.0f});
    bool all = ach_.unlocked("all_grandtour");
    for (auto& g : games_)
        for (const AchievementDef& d : g.module->achievements()) all = all && ach_.unlocked(d.id);
    if (all && ach_.unlock("all_complete")) ach_banners_.push_back({"Completionist", 0.0f});
}

// "Achievement unlocked" slides down at the top, stays a few seconds and fades; several queue up.
void App::draw_ach_banners(float dt) {
    if (ach_banners_.empty()) return;
    AchBanner& b = ach_banners_.front();
    b.t += dt;
    const float kIn = 0.35f, kStay = 3.5f, kOut = 0.5f;
    if (b.t > kIn + kStay + kOut) { ach_banners_.erase(ach_banners_.begin()); return; }
    const float W = (float)video_.width(), H = (float)video_.height();
    const float a = b.t < kIn ? b.t / kIn : b.t > kIn + kStay ? 1.0f - (b.t - kIn - kStay) / kOut : 1.0f;
    const float slide = b.t < kIn ? 1.0f - (1.0f - a) * (1.0f - a) : 1.0f;
    const uint32_t gold = 0xFFF2C14E;
    const std::string head = "\xE2\x98\x85  Achievement unlocked";
    const float lh = video_.line_height(FontId::Small), th = video_.line_height(FontId::Title);
    const float pad = H * 0.022f;
    float name_s = 1.0f;
    float w = std::max(video_.measure_text(FontId::Small, head), video_.measure_text(FontId::Title, b.name)) + pad * 2.4f;
    if (w > W * 0.9f) { name_s = (W * 0.9f - pad * 2.4f) / std::max(1.0f, video_.measure_text(FontId::Title, b.name)); w = W * 0.9f; }
    const float h = lh + th * name_s + pad * 1.6f;
    Rect r = {(W - w) * 0.5f, H * 0.03f - (1.0f - slide) * (h + H * 0.03f), w, h};
    video_.fill_round_rect(r, h * 0.18f, with_alpha(0xF0141824, a));
    video_.outline_round_rect(r, h * 0.18f, std::max(2.0f, H / 360.0f), with_alpha(gold, a));
    video_.draw_text(FontId::Small, r.x + w * 0.5f, r.y + pad * 0.7f, head, with_alpha(gold, a), Align::Center);
    video_.draw_text(FontId::Title, r.x + w * 0.5f, r.y + pad * 0.7f + lh, b.name, with_alpha(0xFFFFFFFF, a), Align::Center, name_s);
}

// The run travels with save states: "a<counts>.<bits>". A state without it (or from before achievements) doesn't
// count as a New Game run.
std::string App::ach_state_tag() const {
    char buf[24];
    snprintf(buf, sizeof(buf), "a%d.%08x", ach_ctx_.run_counts ? 1 : 0, ach_ctx_.run_bits);
    return buf;
}

void App::ach_load_tag(const std::string& tag) {
    AchievementCtx c;
    if (tag.size() >= 3 && tag[0] == 'a') {
        c.run_counts = tag[1] == '1' && settings_.achievements;
        c.run_bits = (uint32_t)strtoul(tag.c_str() + 3, nullptr, 16);
    }
    ach_ctx_ = c;   // the module's frame-to-frame checks start afresh from the loaded point
    if (session_) ach_ctx_.prev.assign(session_->md().ram, session_->md().ram + sizeof(session_->md().ram));
}

MenuPage App::achievements_page(int game) {
    MenuPage p;
    p.title = "Achievements";
    p.wide = true;
    int total = 0, got = 0;
    auto add = [&](const std::string& group, const std::vector<AchievementDef>& defs) {
        p.items.push_back(MenuItem::header(group));
        for (const AchievementDef& d : defs) {
            const bool have = ach_.unlocked(d.id);
            total++;
            got += have ? 1 : 0;
            const bool hidden = d.secret && !have;
            std::string id = d.id;
            const int target = d.target;
            const bool set = d.set;
            MenuItem it = MenuItem::action(hidden ? "???" : d.name, []() {},
                                           hidden ? "Secret achievement: keep playing to find it." : d.desc);
            it.value_text = [this, id, target, set]() {
                std::string on = ach_.unlocked_on(id);
                if (!on.empty()) return std::string("\xE2\x9C\x93  ") + on;
                int have = ach_.progress(id);
                if (set) {   // a set of bits: how many were found
                    int n = 0;
                    for (int b = 0; b < 31; b++) n += (have >> b) & 1;
                    have = n;
                }
                if (target > 0) return std::to_string(std::min(have, target)) + " / " + std::to_string(target);
                return std::string();
            };
            p.items.push_back(it);
        }
    };
    for (int g = 0; g < kGameCount; g++)
        if (game < 0 || g == game) add(games_[g].module->title(), games_[g].module->achievements());
    add("Across the collection", collection_achievements());
    char buf[96];
    snprintf(buf, sizeof(buf), "%d of %d unlocked", got, total);
    p.subtitle = std::string(buf) + (settings_.achievements ? "   \xE2\x80\xA2   on: no cheats, rewind or fast forward"
                                                            : "   \xE2\x80\xA2   off: switch them on under Options to earn them");
    return p;
}

MenuPage App::cheats_locked_page() {
    MenuPage p;
    p.title = "Cheats";
    p.subtitle = "Off while achievements are on";
    p.items.push_back(MenuItem::info("Achievements can only be earned without cheats, rewind or fast forward."));
    p.items.push_back(MenuItem::action("Switch achievements off", [this]() {
        menu_.confirm("Switch achievements off?", [this]() {
            set_achievements(false);
            save_settings();
            menu_.pop();                // (the confirmation has closed itself) this page -> the cheats
            menu_.push(cheats_page());
        }, "Cheats, rewind and fast forward come back. Nothing can be earned while achievements are off.");
    }));
    return p;
}
