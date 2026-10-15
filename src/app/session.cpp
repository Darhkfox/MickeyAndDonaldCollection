#include "session.h"
#include "palette.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

Session::Session() : md_(new MegaDrive) {
    build_palette(lut_, ColorMode::Accurate, 0, 0, 0);
}
Session::~Session() {}

bool Session::open(GameModule* module, const std::vector<uint8_t>& rom) {
    module_ = module;
    if (!md_->load_rom(rom)) return false;
    power_on();
    return true;
}

void Session::apply_settings(const Settings& s) {
    build_palette(lut_, s.color, s.brightness, s.saturation, s.gamma);
    GameConfig c;
    c.ext = wide_ext(s.widescreen);
    if (module_ && c.ext > module_->max_ext()) c.ext = module_->max_ext();   // e.g. World of Illusion tops out at 2:1
    c.hud_anchor = s.hud_anchor;
    if (module_) {
        auto g = s.cheats.find(module_->id());
        if (g != s.cheats.end()) c.cheats = g->second;
    }
    want_ = c;
    md_->vdp.sprite_limit = !s.no_sprite_limit;
    md_->ym.ladder_effect = s.ladder_effect;
    mixer_.configure(md_->sample_rate(), out_rate_, s.audio_filter, s.volume / 100.0f * (module_ ? module_->audio_gain() : 1.0f));
    if (!configured_) switch_config();
    else try_switch();
    configured_ = true;
    last_ = s;
}

void Session::switch_config() {
    cfg_ = want_;
    pending_ = false;
    md_->vdp.ext = cfg_.ext;
    md_->vdp.ignore_left_blank = cfg_.ext > 0;
    if (module_) module_->configure(*md_, cfg_);
}

void Session::try_switch() {
    pending_ = false;
    if (want_.ext == cfg_.ext && want_.hud_anchor == cfg_.hud_anchor && want_.cheats == cfg_.cheats) return;
    if (!module_ || module_->can_configure(*md_, want_)) switch_config();
    else pending_ = true;
}

void Session::power_on() {
    md_->power_on();
    mixer_.reset();
    if (configured_) switch_config();          // the CPU starts from its reset vector: nothing to wait for
    mask_l_ = mask_r_ = (float)cfg_.ext;
}

void Session::soft_reset() {
    md_->soft_reset();
}

bool Session::start_level(int index, int variant) {
    if (!module_) return false;
    bool ok = module_->start_level(*md_, want_, index, variant);   // starts from power-on
    switch_config();
    mixer_.reset();
    return ok;
}

void Session::run_frame(uint16_t pad1, uint16_t pad2) {
    md_->set_pad(0, pad1);
    md_->set_pad(1, pad2);
    if (module_) module_->pre_frame(*md_, cfg_);
    md_->run_frame();
    info_ = FrameInfo();
    if (module_) module_->post_frame(*md_, cfg_, info_);
    if (cfg_.ext == 0) {
        info_.mask_left = info_.mask_right = 0;
    } else if (!info_.gameplay) {
        info_.mask_left = info_.mask_right = cfg_.ext;
        info_.mask_lines = 0;
    }
    mask_lines_ = info_.mask_lines;
    pillar_below_ = info_.pillar_below && info_.gameplay;
    // Leaving gameplay closes the side masks at once (beside a menu the old picture is stale);
    // coming back they open gradually so it doesn't pop. A mask that has to grow (the view widened
    // next to a level edge, a state loaded there) closes at once too: what it covers is outside the level.
    if (!info_.gameplay) mask_l_ = mask_r_ = (float)cfg_.ext;
    const float speed = std::max(2.0f, cfg_.ext / 6.0f);
    auto ease = [&](float& cur, int target) {
        if (cur < target) cur = (float)target;
        else if (cur > target) cur = std::max<float>((float)target, cur - speed);
    };
    ease(mask_l_, std::clamp(info_.mask_left, 0, cfg_.ext));
    ease(mask_r_, std::clamp(info_.mask_right, 0, cfg_.ext));
    if (pending_) try_switch();
}

const uint32_t* Session::frame(int& w, int& h) {
    const VDP& v = md_->vdp;
    w = v.fb_width;
    h = v.fb_lines;
    pixels_.resize((size_t)w * h);
    int ml = (int)(mask_l_ + 0.5f), mr = (int)(mask_r_ + 0.5f);
    for (int y = 0; y < h; y++) {
        const uint16_t* src = v.fb[y];
        uint32_t* dst = &pixels_[(size_t)y * w];
        for (int x = 0; x < w; x++) dst[x] = lut_[src[x] & 0x7FF];
        if (mask_lines_ > 0 && y >= mask_lines_) {
            if (pillar_below_) {
                for (int x = 0; x < cfg_.ext && x < w; x++) dst[x] = 0xFF000000u;
                for (int x = std::max(0, w - cfg_.ext); x < w; x++) dst[x] = 0xFF000000u;
            }
            continue;
        }
        for (int x = 0; x < ml && x < w; x++) dst[x] = 0xFF000000u;
        for (int x = std::max(0, w - mr); x < w; x++) dst[x] = 0xFF000000u;
    }
    return pixels_.data();
}

void Session::set_audio_rate(double out_rate) {
    out_rate_ = out_rate;
    mixer_.configure(md_->sample_rate(), out_rate_, last_.audio_filter, last_.volume / 100.0f * (module_ ? module_->audio_gain() : 1.0f));
}

void Session::mix_audio(std::vector<int16_t>& out, double adjust) {
    if (muted) return;
    mixer_.process(md_->audio.data(), md_->audio_frames, adjust, out);
}

std::vector<uint8_t> Session::save_state() {
    std::vector<uint8_t> s = md_->save_state();
    // Append a tag: the module id (states from another game are rejected) and the configuration the ROM ran
    // with ("qs|108|cheat,cheat"), so a load can restore exactly that ROM before moving to the current settings.
    std::string tag = module_ ? module_->id() : "";
    tag += "|" + std::to_string(cfg_.ext) + "|";
    for (const auto& kv : cfg_.cheats)
        if (kv.second && tag.size() + kv.first.size() < 250) tag += kv.first + ",";
    s.insert(s.end(), tag.begin(), tag.end());
    s.push_back((uint8_t)tag.size());
    return s;
}

bool Session::load_state(const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    size_t idlen = data.back();
    if (data.size() < idlen + 1) return false;
    std::string tag(data.end() - 1 - idlen, data.end() - 1);
    size_t bar = tag.find('|');
    if (!module_ || tag.substr(0, bar) != module_->id()) return false;
    std::vector<uint8_t> core(data.begin(), data.end() - 1 - idlen);
    if (!md_->load_state(core)) return false;
    // The state runs on the ROM it was saved with; the current settings follow when it is safe (try_switch).
    // States without the configuration (older builds) take the current settings at once.
    GameConfig saved = want_;
    if (bar != std::string::npos) {
        size_t bar2 = tag.find('|', bar + 1);
        saved.ext = std::clamp(atoi(tag.c_str() + bar + 1), 0, module_->max_ext());
        saved.cheats.clear();
        std::string list = bar2 == std::string::npos ? "" : tag.substr(bar2 + 1);
        for (size_t a = 0; a < list.size();) {
            size_t b = list.find(',', a);
            if (b == std::string::npos) b = list.size();
            if (b > a) saved.cheats[list.substr(a, b - a)] = true;
            a = b + 1;
        }
    }
    const GameConfig want = want_;
    want_ = saved;
    switch_config();
    want_ = want;
    try_switch();
    mixer_.reset();
    return true;
}
