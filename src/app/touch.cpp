#include "touch.h"
#include "ui.h"
#include "../core/md.h"
#include <algorithm>
#include <cmath>

// Layout, in units of u = 1/8 of the screen height: the D-pad bottom-left, A B C rising to the right like the
// Mega Drive pad's, Start top centre, Menu top-left, fast forward / rewind top-right.
void TouchControls::layout(float W, float H) {
    if (W == W_ && H == H_) return;
    W_ = W; H_ = H;
    u_ = H / 8.0f;
    const float u = u_;
    pad_r_ = u * 1.45f;
    pad_x_ = u * 0.55f + pad_r_;
    pad_y_ = H - u * 0.45f - pad_r_;
    const float br = u * 0.62f, bx = W - u * 2.6f, by = H - u * 1.65f;
    btn_[BtnA] = {bx - u * 1.5f, by + u * 0.42f, br};
    btn_[BtnB] = {bx, by, br};
    btn_[BtnC] = {bx + u * 1.5f, by - u * 0.42f, br};
    btn_[BtnStart] = {W * 0.5f, u * 0.6f, u * 0.42f};   // top centre: the games' status bars are at the bottom
    btn_[BtnMenu] = {u * 0.75f, u * 0.6f, u * 0.42f};
    btn_[BtnFF] = {W - u * 0.75f, u * 0.6f, u * 0.42f};
    btn_[BtnRewind] = {W - u * 1.85f, u * 0.6f, u * 0.42f};
}

TouchControls::Finger* TouchControls::find(int64_t id) {
    for (auto& f : fingers_) if (f.id == id) return &f;
    return nullptr;
}

int TouchControls::control_at(float x, float y) const {
    if (!in_game) return -1;
    for (int b = 0; b < BtnCount; b++) {
        if ((b == BtnFF && !show_ff) || (b == BtnRewind && !show_rewind)) continue;
        const Circle& c = btn_[b];
        float reach = c.r * (b <= BtnC ? 1.35f : 1.5f);   // a little forgiving around each button
        if (std::hypot(x - c.x, y - c.y) <= reach) return b;
    }
    if (std::hypot(x - pad_x_, y - pad_y_) <= pad_r_ * 1.5f) return -2;
    return -1;
}

void TouchControls::on_down(int64_t id, float x, float y) {
    Finger* f = find(id);
    if (!f) f = find(-1);
    if (!f) return;
    f->id = id;
    f->x0 = f->x = x; f->y0 = f->y = f->last_y = y;
    f->moved = false;
    f->ctl = control_at(x, y);
    if (f->ctl >= 0) pressed_[f->ctl] = true;
}

void TouchControls::on_move(int64_t id, float x, float y) {
    Finger* f = find(id);
    if (!f) return;
    // a face-button finger that slides onto another face button presses that one instead (rolling thumb)
    if (f->ctl >= BtnA && f->ctl <= BtnC) {
        for (int b = BtnA; b <= BtnC; b++)
            if (b != f->ctl && std::hypot(x - btn_[b].x, y - btn_[b].y) <= btn_[b].r * 1.1f) {
                bool was = button_held(b);
                f->ctl = b;
                if (!was) pressed_[b] = true;
                break;
            }
    }
    if (f->ctl == -1) {
        drag_ += y - f->last_y;
        f->last_y = y;
    }
    f->x = x; f->y = y;
    if (std::hypot(x - f->x0, y - f->y0) > u_ * 0.25f) f->moved = true;
}

void TouchControls::on_up(int64_t id, float x, float y) {
    Finger* f = find(id);
    if (!f) return;
    if (f->ctl == -1 && !f->moved) { tap_ = true; tap_x_ = x; tap_y_ = y; }
    *f = Finger();
}

void TouchControls::release_all() {
    for (auto& f : fingers_) f = Finger();
    for (bool& p : pressed_) p = false;
    tap_ = false;
    drag_ = 0;
}

bool TouchControls::button_held(int b) const {
    for (const auto& f : fingers_) if (f.id != -1 && f.ctl == b) return true;
    return false;
}

uint16_t TouchControls::dpad_bits(float x, float y) const {
    float dx = x - pad_x_, dy = y - pad_y_;
    if (std::hypot(dx, dy) < pad_r_ * 0.22f) return 0;
    // 8 directions, 45-degree sectors: diagonals press both
    float a = std::atan2(dy, dx) * 57.29578f;    // -180..180, 0 = right, 90 = down
    uint16_t b = 0;
    if (a > -67.5f && a < 67.5f) b |= MD_RIGHT;
    if (a > 112.5f || a < -112.5f) b |= MD_LEFT;
    if (a > 22.5f && a < 157.5f) b |= MD_DOWN;
    if (a < -22.5f && a > -157.5f) b |= MD_UP;
    return b;
}

uint16_t TouchControls::md_buttons() const {
    if (!in_game) return 0;
    uint16_t s = 0;
    for (const auto& f : fingers_) {
        if (f.id == -1) continue;
        if (f.ctl == -2) s |= dpad_bits(f.x, f.y);
        else if (f.ctl == BtnA) s |= MD_A;
        else if (f.ctl == BtnB) s |= MD_B;
        else if (f.ctl == BtnC) s |= MD_C;
        else if (f.ctl == BtnStart) s |= MD_START;
    }
    return s;
}

bool TouchControls::held(Button b) const { return in_game && button_held(b); }

bool TouchControls::take_pressed(Button b) {
    bool p = pressed_[b];
    pressed_[b] = false;
    return p;
}

bool TouchControls::take_tap(float& x, float& y) {
    if (!tap_) return false;
    tap_ = false;
    x = tap_x_; y = tap_y_;
    return true;
}

float TouchControls::take_drag() {
    float d = drag_;
    drag_ = 0;
    return d;
}

void TouchControls::draw(Video& v, float opacity, bool ff, bool rw) const {
    if (!in_game || !visible || opacity <= 0.01f) return;
    const float u = u_;
    auto a = [&](float k) { return with_alpha(0xFFFFFFFF, std::min(1.0f, k * opacity)); };
    auto dark = [&](float k) { return with_alpha(0xFF0B0E16, std::min(1.0f, k * opacity)); };
    const float line = std::max(2.0f, u * 0.03f);

    // D-pad: a disc with a cross, the held directions lit
    uint16_t held_dirs = 0;
    for (const auto& f : fingers_) if (f.id != -1 && f.ctl == -2) held_dirs |= dpad_bits(f.x, f.y);
    v.fill_round_rect({pad_x_ - pad_r_, pad_y_ - pad_r_, pad_r_ * 2, pad_r_ * 2}, pad_r_, dark(0.35f));
    v.outline_round_rect({pad_x_ - pad_r_, pad_y_ - pad_r_, pad_r_ * 2, pad_r_ * 2}, pad_r_, line, a(0.35f));
    const float arm = pad_r_ * 0.36f, len = pad_r_ * 0.86f;
    v.fill_round_rect({pad_x_ - arm, pad_y_ - len, arm * 2, len * 2}, arm * 0.35f, a(0.22f));
    v.fill_round_rect({pad_x_ - len, pad_y_ - arm, len * 2, arm * 2}, arm * 0.35f, a(0.22f));
    struct { uint16_t bit; float dx, dy; } dirs[] = {{MD_UP, 0, -1}, {MD_DOWN, 0, 1}, {MD_LEFT, -1, 0}, {MD_RIGHT, 1, 0}};
    for (auto& d : dirs) {
        if (!(held_dirs & d.bit)) continue;
        float cx = pad_x_ + d.dx * len * 0.55f, cy = pad_y_ + d.dy * len * 0.55f;
        v.fill_round_rect({cx - arm, cy - arm, arm * 2, arm * 2}, arm * 0.35f, a(0.45f));
    }

    // round buttons
    auto round_button = [&](int b, const char* label, FontId font) {
        const Circle& c = btn_[b];
        bool on = button_held(b);
        v.fill_round_rect({c.x - c.r, c.y - c.r, c.r * 2, c.r * 2}, c.r, on ? a(0.45f) : dark(0.4f));
        v.outline_round_rect({c.x - c.r, c.y - c.r, c.r * 2, c.r * 2}, c.r, line, a(on ? 0.8f : 0.45f));
        float s = std::min(1.0f, c.r * 1.5f / std::max(1.0f, v.measure_text(font, label)));
        v.draw_text(font, c.x, c.y - v.line_height(font, s) * 0.52f, label, a(0.85f), Align::Center, s);
    };
    round_button(BtnA, "A", FontId::Title);
    round_button(BtnB, "B", FontId::Title);
    round_button(BtnC, "C", FontId::Title);
    round_button(BtnStart, "START", FontId::Small);
    round_button(BtnMenu, "MENU", FontId::Small);
    if (ff) round_button(BtnFF, "\xE2\x96\xB6\xE2\x96\xB6", FontId::Small);
    if (rw) round_button(BtnRewind, "\xE2\x97\x80\xE2\x97\x80", FontId::Small);
}
