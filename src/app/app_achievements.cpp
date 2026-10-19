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
    if (settings_.achievements) ach_welcome();
}

// Once, on the first start: what achievements are and what they switch off.
void App::ach_welcome() {
    int total = (int)collection_achievements().size();
    for (auto& g : games_) total += (int)g.module->achievements().size();
    MenuPage p;
    p.title = "Achievements";
    p.subtitle = std::to_string(total) + " to earn across Castle of Illusion, QuackShot and World of Illusion";
    p.wide = true;
    p.items.push_back(MenuItem::info("Beat the bosses, clear the worlds, pull off tricky moves and find the secret ones."));
    p.items.push_back(MenuItem::header("To keep them fair, while achievements are on"));
    p.items.push_back(MenuItem::info("Cheats, Level Select, Fast Forward and Rewind are switched off."));
    p.items.push_back(MenuItem::info("Save states still work. Turn achievements off any time on the Achievements page."));
    p.items.push_back(MenuItem::header(""));
    p.items.push_back(MenuItem::action("Play with achievements", [this]() { menu_.close(); },
                                       "Each one pops up as you earn it; the launcher's Achievements page lists them all."));
    p.items.push_back(MenuItem::action("Turn achievements off", [this]() {
        set_achievements(false);
        save_settings();
        menu_.close();
    }, "Cheats, Level Select, Fast Forward and Rewind stay available. Turn achievements on again on their page."));
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
    // Achievements only count in a game that started with them on (launch or Reset Game): switched on mid-game,
    // what cheats, the level select or rewind did in this game can't earn anything.
    ach_ctx_.run_counts = false;
    ach_ctx_.reset_scratch();
    if (on) rewind_.clear();
    if (on && mode_ == Mode::Playing) menu_.toast("Achievements will count from your next Reset Game", 4.0f);
    apply_settings();   // cheats leave (or come back into) the running game
}

void App::ach_frame() {
    if (!ach_ctx_.run_counts) return;   // this game didn't start with achievements on
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
        if (ok(i)) ach_award(defs[i].id, defs[i].name, defs[i].desc);
    c.unlocked.clear();
    c.progress.clear();
    c.counted.clear();
    c.marked.clear();
    if (dirty) ach_.save();
}

void App::ach_award(const std::string& id, const std::string& name, const std::string& desc) {
    if (!ach_.unlock(id)) return;
    ach_banners_.push_back({name, desc, 0.0f});
    ach_check_collection();
    ach_.save();
}

// Grand Tour (finish all three games) and Completionist (everything else).
void App::ach_check_collection() {
    auto banner = [&](const char* id) {
        for (const AchievementDef& d : collection_achievements())
            if (std::string(d.id) == id) ach_banners_.push_back({d.name, d.desc, 0.0f});
    };
    int finished = 0;
    for (const char* id : kGrandTourGames) finished += ach_.unlocked(id) ? 1 : 0;
    ach_.set_progress("all_grandtour", finished);
    if (finished == 3 && ach_.unlock("all_grandtour")) banner("all_grandtour");
    bool all = ach_.unlocked("all_grandtour");
    for (auto& g : games_)
        for (const AchievementDef& d : g.module->achievements()) all = all && ach_.unlocked(d.id);
    if (all && ach_.unlock("all_complete")) banner("all_complete");
}

// "Achievement unlocked": a small card (a star, the name and what it was for) slides down at the top, stays a few
// seconds and fades; several queue up.
void App::draw_ach_banners(float dt) {
    if (ach_banners_.empty()) return;
    AchBanner& b = ach_banners_.front();
    b.t += dt;
    const float kIn = 0.35f, kStay = 4.0f, kOut = 0.5f;
    if (b.t > kIn + kStay + kOut) { ach_banners_.erase(ach_banners_.begin()); return; }
    const float W = (float)video_.width(), H = (float)video_.height();
    const float a = b.t < kIn ? b.t / kIn : b.t > kIn + kStay ? 1.0f - (b.t - kIn - kStay) / kOut : 1.0f;
    const float slide = b.t < kIn ? 1.0f - (1.0f - a) * (1.0f - a) : 1.0f;
    const uint32_t gold = 0xFFF2C14E;
    const std::string head = "Achievement unlocked";
    const float ls = video_.line_height(FontId::Small), lb = video_.line_height(FontId::Body);
    const float pad = ls * 0.55f;
    const float text_h = ls + lb + (b.desc.empty() ? 0.0f : ls);
    const float icon = text_h * 0.78f;
    // lines too long for the screen shrink to fit
    const float max_tw = W * 0.86f - icon - pad * 3.0f;
    auto fit = [&](FontId f, const std::string& s) { return std::min(1.0f, max_tw / std::max(1.0f, video_.measure_text(f, s))); };
    const float name_s = fit(FontId::Body, b.name), desc_s = fit(FontId::Small, b.desc);
    const float tw = std::max({video_.measure_text(FontId::Small, head), video_.measure_text(FontId::Body, b.name) * name_s,
                               video_.measure_text(FontId::Small, b.desc) * desc_s});
    const float w = icon + tw + pad * 3.0f, h = text_h + pad * 1.6f;
    Rect r = {(W - w) * 0.5f, H * 0.025f - (1.0f - slide) * (h + H * 0.025f), w, h};
    video_.fill_round_rect(r, h * 0.2f, with_alpha(0xF0141824, a));
    video_.outline_round_rect(r, h * 0.2f, std::max(1.5f, H / 480.0f), with_alpha(gold, a));
    // the badge: a gold star in a gold ring
    const Rect ic = {r.x + pad * 1.1f, r.y + (h - icon) * 0.5f, icon, icon};
    video_.fill_round_rect(ic, icon * 0.5f, with_alpha(0xFF262B38, a));
    video_.outline_round_rect(ic, icon * 0.5f, std::max(1.5f, icon * 0.06f), with_alpha(gold, a));
    video_.fill_star(ic.x + icon * 0.5f, ic.y + icon * 0.5f, icon * 0.36f, with_alpha(gold, a));
    const float tx = ic.x + icon + pad;
    float y = r.y + pad * 0.8f;
    video_.draw_text(FontId::Small, tx, y, head, with_alpha(gold, a));
    y += ls;
    video_.draw_text(FontId::Body, tx, y, b.name, with_alpha(0xFFFFFFFF, a), Align::Left, name_s);
    y += lb;
    if (!b.desc.empty()) video_.draw_text(FontId::Small, tx, y, b.desc, with_alpha(0xFFB7BFCE, a), Align::Left, desc_s);
}

// Whether achievements count travels with save states: "a<counts>.<bits>". A state saved while they didn't count
// (or from before achievements) doesn't count either, until the next Reset Game.
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
    if (settings_.achievements && !c.run_counts)
        menu_.toast("This save state was made without achievements: they count again after Reset Game", 4.0f);
}

MenuPage App::achievements_page(int game) {
    MenuPage p;
    p.title = "Achievements";
    p.wide = true;
    p.items.push_back(achievements_switch());
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
    p.subtitle = buf;
    return p;
}

// The Achievements on / off switch (Options, and the top of the Achievements page). Switching off during a game that
// counts asks first: that game can't earn any more after it.
MenuItem App::achievements_switch() {
    MenuItem it = MenuItem::toggle("Achievements", [this]() { return settings_.achievements; }, [this](bool on) {
        if (on || mode_ != Mode::Playing || !ach_ctx_.run_counts) {
            set_achievements(on);
            save_settings();
            return;
        }
        menu_.confirm("Turn achievements off?", [this]() {
            set_achievements(false);
            save_settings();
        }, "Cheats, Level Select, Fast Forward and Rewind come back. Achievements count again after a Reset Game.");
    }, "On: earn achievements; cheats, level select, fast forward and rewind are off. Off: those come back.");
    it.value_text = [this]() {
        return std::string(settings_.achievements && mode_ == Mode::Playing && !ach_ctx_.run_counts ? "from your next Reset Game" : "");
    };
    return it;
}
