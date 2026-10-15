#include "ui.h"
#include <algorithm>
#include <cmath>

uint32_t with_alpha(uint32_t c, float a) {
    uint32_t al = (uint32_t)std::clamp(((c >> 24) & 0xFF) * a, 0.0f, 255.0f);
    return (al << 24) | (c & 0xFFFFFF);
}
uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = ((a >> s) & 0xFF) + (((float)((b >> s) & 0xFF)) - ((a >> s) & 0xFF)) * t;
        out |= (uint32_t)std::clamp(x, 0.0f, 255.0f) << s;
    }
    return out;
}

void draw_panel(Video& v, Rect r, uint32_t accent, float alpha) {
    float rad = v.height() * 0.018f;
    Rect sh = {r.x + rad * 0.3f, r.y + rad * 0.5f, r.w, r.h};
    v.fill_round_rect(sh, rad, with_alpha(0x80000000, alpha));
    v.fill_round_rect(r, rad, with_alpha(0xEA12151C, alpha));
    Rect top = {r.x, r.y, r.w, r.h * 0.5f};
    (void)top;
    v.outline_round_rect(r, rad, std::max(1.5f, v.height() / 540.0f), with_alpha(lerp_color(accent, 0xFF12151C, 0.35f), alpha));
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------
MenuItem MenuItem::action(std::string label, std::function<void()> fn, std::string hint) {
    MenuItem m; m.kind = Action; m.label = std::move(label); m.on_activate = std::move(fn); m.hint = std::move(hint);
    return m;
}
MenuItem MenuItem::toggle(std::string label, std::function<bool()> get, std::function<void(bool)> set, std::string hint) {
    MenuItem m; m.kind = Toggle; m.label = std::move(label); m.get_bool = std::move(get); m.set_bool = std::move(set);
    m.hint = std::move(hint);
    return m;
}
MenuItem MenuItem::choice(std::string label, std::vector<std::string> choices, std::function<int()> get,
                          std::function<void(int)> set, std::string hint) {
    MenuItem m; m.kind = Choice; m.label = std::move(label); m.choices = std::move(choices);
    m.get_int = std::move(get); m.set_int = std::move(set); m.hint = std::move(hint);
    return m;
}
MenuItem MenuItem::slider(std::string label, int mn, int mx, int step, std::function<int()> get,
                          std::function<void(int)> set, std::function<std::string(int)> fmt, std::string hint) {
    MenuItem m; m.kind = Slider; m.label = std::move(label); m.min = mn; m.max = mx; m.step = step;
    m.get_int = std::move(get); m.set_int = std::move(set); m.format = std::move(fmt); m.hint = std::move(hint);
    return m;
}
MenuItem MenuItem::header(std::string label) { MenuItem m; m.kind = Header; m.label = std::move(label); return m; }
MenuItem MenuItem::info(std::string label, std::function<std::string()> value) {
    MenuItem m; m.kind = Info; m.label = std::move(label); m.value_text = std::move(value); return m;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void Menu::open(MenuPage page) { stack_.clear(); push(std::move(page)); }
void Menu::push(MenuPage page) { fix_selection(page); stack_.push_back(std::move(page)); capture_ = false; }
void Menu::pop() {
    if (stack_.empty()) return;
    auto cb = stack_.back().on_back;
    stack_.pop_back();
    capture_ = false;
    if (cb) cb();
}
void Menu::close() { stack_.clear(); capture_ = false; }

void Menu::fix_selection(MenuPage& p) {
    auto ok = [&](int i) {
        const MenuItem& it = p.items[i];
        return it.selectable() && (!it.enabled || it.enabled());
    };
    if (p.sel >= 0 && p.sel < (int)p.items.size() && ok(p.sel)) return;
    p.sel = -1;
    for (int i = 0; i < (int)p.items.size(); i++) if (ok(i)) { p.sel = i; break; }
}

void Menu::move(MenuPage& p, int dir) {
    int n = (int)p.items.size();
    if (n == 0) return;
    int i = p.sel;
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        const MenuItem& it = p.items[i];
        if (it.selectable() && (!it.enabled || it.enabled())) { p.sel = i; return; }
    }
}

void Menu::update(const UiInput& in) {
    if (stack_.empty()) return;
    MenuPage& p = stack_.back();
    if (p.on_frame) p.on_frame();
    if (capture_) {
        if (capture_block_ > 0) { capture_block_ -= 1; return; }
        MenuItem& it = p.items[p.sel];
        if (it.bind_pad) {
            if (in.key_pressed == 0x1B) { capture_ = false; return; }   // Esc cancels
            if (in.pad_pressed) {
                uint32_t b = in.pad_pressed & (~in.pad_pressed + 1);     // lowest set bit
                if (it.on_bind) it.on_bind(0, b);
                capture_ = false;
            }
        } else if (in.key_pressed) {
            if (in.key_pressed != 0x1B && it.on_bind) it.on_bind(in.key_pressed, 0);
            capture_ = false;
        }
        return;
    }
    fix_selection(p);
    if (in.back || in.menu) { pop(); return; }
    if (in.up) move(p, -1);
    if (in.down) move(p, 1);
    if (p.sel < 0) return;
    MenuItem& it = p.items[p.sel];
    switch (it.kind) {
    case MenuItem::Action:
        if (in.confirm && it.on_activate) { auto fn = it.on_activate; fn(); }   // may restructure the stack
        return;
    case MenuItem::Toggle:
        if ((in.confirm || in.left || in.right) && it.set_bool) it.set_bool(!it.get_bool());
        break;
    case MenuItem::Choice: {
        int n = (int)it.choices.size();
        if (n == 0) break;
        int cur = it.get_int ? it.get_int() : 0;
        if (in.left) it.set_int((cur - 1 + n) % n);
        else if (in.right || in.confirm) it.set_int((cur + 1) % n);
        break;
    }
    case MenuItem::Slider: {
        int cur = it.get_int ? it.get_int() : 0;
        if (in.left) it.set_int(std::max(it.min, cur - it.step));
        else if (in.right) it.set_int(std::min(it.max, cur + it.step));
        break;
    }
    case MenuItem::Bind:
        if (in.confirm) { capture_ = true; capture_block_ = 2; }
        break;
    default: break;
    }
}

void Menu::draw(Video& v, float time) {
    if (stack_.empty()) return;
    MenuPage& p = stack_.back();
    const float W = (float)v.width(), H = (float)v.height();
    v.fill_rect({0, 0, W, H}, 0x9A05070C);

    float pw = std::min(W * (p.wide ? 0.78f : 0.62f), H * (p.wide ? 1.45f : 1.15f));
    float ph = H * 0.84f;
    Rect panel = {(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
    draw_panel(v, panel, accent);

    const float pad = H * 0.035f;
    // Long titles / hints shrink to fit the panel instead of running out of it.
    auto fit = [&](FontId f, const std::string& t) { return std::min(1.0f, (pw - pad * 2) / std::max(1.0f, v.measure_text(f, t))); };
    float y = panel.y + pad * 0.8f;
    // Breadcrumb for nested pages
    if (stack_.size() > 1) {
        std::string crumb;
        for (size_t i = 0; i + 1 < stack_.size(); i++) crumb += stack_[i].title + "  \xC2\xBB  ";
        v.draw_text(FontId::Small, panel.x + pad, y, crumb, 0xFF8A93A6, Align::Left, fit(FontId::Small, crumb));
        y += v.line_height(FontId::Small);
    }
    v.draw_text(FontId::Title, panel.x + pad, y, p.title, accent, Align::Left, fit(FontId::Title, p.title));
    y += v.line_height(FontId::Title);
    if (!p.subtitle.empty()) {
        v.draw_text(FontId::Small, panel.x + pad, y - v.line_height(FontId::Small) * 0.15f, p.subtitle, 0xFFB7BFCE, Align::Left,
                    fit(FontId::Small, p.subtitle));
        y += v.line_height(FontId::Small);
    }
    y += pad * 0.35f;
    v.fill_rect({panel.x + pad, y, panel.w - pad * 2, std::max(1.0f, H / 720.0f)}, with_alpha(accent, 0.35f));
    y += pad * 0.45f;

    const float row = v.line_height(FontId::Body) * 1.32f;
    const float footer_h = v.line_height(FontId::Small) * 3.2f;
    const float list_top = y, list_bottom = panel.y + panel.h - footer_h - pad * 0.5f;
    const int visible = std::max(1, (int)((list_bottom - list_top) / row));
    // Keep the selection in view (smooth scroll).
    float target = p.scroll;
    if (p.sel >= 0) {
        if (p.sel < target + 1) target = (float)std::max(0, p.sel - 1);
        if (p.sel > target + visible - 2) target = (float)(p.sel - visible + 2);
        // On the last selectable row, also bring the read-only rows after it (e.g. Options' Hotkeys list) into
        // view, as far as the selection stays on screen.
        int last = -1;
        for (int i = (int)p.items.size() - 1; i >= 0 && last < 0; i--)
            if (p.items[i].selectable()) last = i;
        if (p.sel == last) target = std::max(target, std::min((float)p.sel, (float)((int)p.items.size() - visible)));
    }
    target = std::clamp(target, 0.0f, (float)std::max(0, (int)p.items.size() - visible));
    p.scroll += (target - p.scroll) * 0.35f;
    if (std::fabs(target - p.scroll) < 0.01f) p.scroll = target;

    const float tx = panel.x + pad, vx = panel.x + panel.w - pad;
    int first = (int)std::floor(p.scroll);
    for (int i = first; i < (int)p.items.size() && i < first + visible + 1; i++) {
        float ry = list_top + (i - p.scroll) * row;
        if (ry < list_top - row * 0.5f || ry + row > list_bottom + row * 0.5f) continue;
        const MenuItem& it = p.items[i];
        bool en = !it.enabled || it.enabled();
        bool sel = i == p.sel;
        float ty = ry + (row - v.line_height(FontId::Body)) * 0.5f;
        if (it.kind == MenuItem::Header) {
            v.draw_text(FontId::Small, tx, ry + row - v.line_height(FontId::Small) * 1.1f, it.label, with_alpha(accent, 0.9f));
            continue;
        }
        if (sel) {
            float pulse = 0.22f + 0.06f * std::sin(time * 4.0f);
            Rect hr = {panel.x + pad * 0.5f, ry + row * 0.06f, panel.w - pad, row * 0.88f};
            v.fill_round_rect(hr, row * 0.2f, with_alpha(accent, pulse));
            v.fill_round_rect({hr.x, hr.y + hr.h * 0.18f, std::max(3.0f, H / 300.0f), hr.h * 0.64f}, 2.0f, accent);
        }
        uint32_t col = !en ? 0xFF5A6070 : sel ? 0xFFFFFFFF : 0xFFD4DAE6;
        v.draw_text(FontId::Body, tx + (sel ? pad * 0.35f : 0), ty, it.label, col);
        std::string val;
        uint32_t vcol = !en ? 0xFF5A6070 : sel ? 0xFFFFFFFF : 0xFFA9B3C6;
        switch (it.kind) {
        case MenuItem::Toggle: {
            bool on = it.get_bool && it.get_bool();
            // pill switch
            float sh = v.line_height(FontId::Body) * 0.62f, sw = sh * 1.9f;
            Rect sr = {vx - sw, ry + (row - sh) * 0.5f, sw, sh};
            v.fill_round_rect(sr, sh * 0.5f, on ? with_alpha(accent, en ? 1.0f : 0.4f) : 0xFF3A404E);
            float kx = on ? sr.x + sw - sh : sr.x;
            v.fill_round_rect({kx + sh * 0.12f, sr.y + sh * 0.12f, sh * 0.76f, sh * 0.76f}, sh * 0.38f, 0xFFF4F6FA);
            break;
        }
        case MenuItem::Choice: {
            int c = it.get_int ? it.get_int() : 0;
            if (c >= 0 && c < (int)it.choices.size()) val = it.choices[c];
            if (sel) val = "\xE2\x97\x80  " + val + "  \xE2\x96\xB6";
            break;
        }
        case MenuItem::Slider: {
            int c = it.get_int ? it.get_int() : 0;
            float bw = pw * 0.22f, bh = std::max(3.0f, H / 200.0f);
            std::string txt = it.format ? it.format(c) : std::to_string(c);
            float tw = v.measure_text(FontId::Body, "00000") ;
            Rect br = {vx - bw - tw, ry + row * 0.5f - bh * 0.5f, bw, bh};
            v.fill_round_rect(br, bh * 0.5f, 0xFF3A404E);
            float f = it.max > it.min ? (float)(c - it.min) / (it.max - it.min) : 0;
            v.fill_round_rect({br.x, br.y, br.w * f, bh}, bh * 0.5f, en ? accent : 0xFF5A6070);
            v.fill_round_rect({br.x + br.w * f - bh * 1.4f, br.y - bh * 0.9f, bh * 2.8f, bh * 2.8f}, bh * 1.4f, 0xFFF4F6FA);
            v.draw_text(FontId::Body, vx, ty, txt, vcol, Align::Right);
            break;
        }
        case MenuItem::Bind:
            val = capture_ && sel ? (it.bind_pad ? "Press a button\xE2\x80\xA6" : "Press a key\xE2\x80\xA6") :
                  (it.value_text ? it.value_text() : "");
            if (capture_ && sel) vcol = accent;
            break;
        default:
            if (it.value_text) val = it.value_text();
            break;
        }
        if (!val.empty()) v.draw_text(FontId::Body, vx, ty, val, vcol, Align::Right);
    }
    // scroll indicators
    if (p.scroll > 0.05f) v.draw_text(FontId::Small, panel.x + panel.w * 0.5f, list_top - v.line_height(FontId::Small) * 0.9f, "\xE2\x86\x91", 0xFF8A93A6, Align::Center);
    if (p.scroll + visible < (float)p.items.size() - 0.05f) v.draw_text(FontId::Small, panel.x + panel.w * 0.5f, list_bottom - v.line_height(FontId::Small) * 0.2f, "\xE2\x86\x93", 0xFF8A93A6, Align::Center);

    // footer: hint + controls
    float fy = panel.y + panel.h - footer_h;
    v.fill_rect({panel.x + pad, fy, panel.w - pad * 2, std::max(1.0f, H / 720.0f)}, 0x30FFFFFF);
    if (p.sel >= 0 && !p.items[p.sel].hint.empty()) {
        const std::string& hint = p.items[p.sel].hint;
        v.draw_text(FontId::Small, tx, fy + v.line_height(FontId::Small) * 0.35f, hint, 0xFFC9D0DC, Align::Left, fit(FontId::Small, hint));
    }
    v.draw_text(FontId::Small, tx, fy + v.line_height(FontId::Small) * 1.75f, footer, 0xFF7C8598);
}

void Menu::toast(const std::string& msg, float seconds) {
    toasts_.push_back({msg, 0.0f, seconds});
    if (toasts_.size() > 4) toasts_.erase(toasts_.begin());
}

float Menu::toast_cover() const {
    float a = 0.0f;
    for (const Toast& t : toasts_) a = std::max(a, std::min(1.0f, std::min(t.t * 6.0f, (t.life - t.t) * 3.0f)));
    return std::max(0.0f, a);
}

void Menu::draw_toasts(Video& v, float dt, bool bottom) {
    const float H = (float)v.height(), W = (float)v.width();
    // In play: top-right, stacking down. Elsewhere (launcher, menus, music player) the top of the
    // screen holds titles, so toasts sit bottom-centre over the key hints, stacking up.
    const FontId font = bottom ? FontId::Small : FontId::Body;
    const float h = v.line_height(font) * 1.5f;
    float y = bottom ? H - h - H * 0.01f : H * 0.06f;
    for (Toast& t : toasts_) t.t += dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.t > t.life; }), toasts_.end());
    for (size_t i = 0; i < toasts_.size(); i++) {
        const Toast& t = toasts_[bottom ? toasts_.size() - 1 - i : i];   // newest nearest the edge
        float a = std::min(1.0f, std::min(t.t * 6.0f, (t.life - t.t) * 3.0f));
        float tw = v.measure_text(font, t.text);
        float bw = tw + H * 0.05f;
        Rect r = {bottom ? (W - bw) * 0.5f : W - tw - H * 0.09f, y, bw, h};
        v.fill_round_rect(r, h * 0.3f, with_alpha(0xF0181C26, a));
        v.fill_round_rect({r.x, r.y + h * 0.2f, std::max(3.0f, H / 300.0f), h * 0.6f}, 2.0f, with_alpha(accent, a));
        v.draw_text(font, r.x + H * 0.025f, r.y + h * 0.17f, t.text, with_alpha(0xFFFFFFFF, a));
        y += bottom ? -h * 1.15f : h * 1.15f;
    }
}

void Menu::confirm(const std::string& question, std::function<void()> yes, const std::string& detail) {
    MenuPage p;
    p.title = question;
    p.subtitle = detail;
    p.items.push_back(MenuItem::action("Yes", [this, yes]() { auto y = yes; pop(); y(); }));
    p.items.push_back(MenuItem::action("No", [this]() { pop(); }));
    p.sel = 1;
    push(std::move(p));
}
