// On-screen controls for touch screens (Android): a Mega Drive pad (D-pad, A B C, Start) over the game, plus
// Menu, fast forward and rewind buttons; and taps / drags for the launcher, menus and music player.
// Positions are in backbuffer pixels.
#pragma once
#include <cstdint>
#include "../platform/video.h"

class TouchControls {
public:
    enum Button { BtnA, BtnB, BtnC, BtnStart, BtnMenu, BtnFF, BtnRewind, BtnCount };

    void on_down(int64_t finger, float x, float y);
    void on_move(int64_t finger, float x, float y);
    void on_up(int64_t finger, float x, float y);
    void release_all();

    void layout(float W, float H);
    // Mega Drive pad bits (MD_UP ... MD_START) held on the on-screen pad.
    uint16_t md_buttons() const;
    bool held(Button b) const;
    bool take_pressed(Button b);           // went down since the last call
    // A short tap anywhere (for the launcher and menus), and vertical dragging since the last call.
    bool take_tap(float& x, float& y);
    float take_drag();

    void draw(Video& v, float opacity, bool show_ff, bool show_rewind) const;

    bool visible = true;                   // shown while touch is the input in use
    bool in_game = false;                  // the on-screen pad only exists during play
    bool show_ff = true, show_rewind = true;

private:
    struct Finger { int64_t id = -1; int ctl = -1; float x0 = 0, y0 = 0, x = 0, y = 0, last_y = 0; bool moved = false; };
    static const int kFingers = 10;
    Finger fingers_[kFingers];
    float W_ = 0, H_ = 0, u_ = 0;
    float pad_x_ = 0, pad_y_ = 0, pad_r_ = 0;
    struct Circle { float x, y, r; } btn_[BtnCount] = {};
    bool pressed_[BtnCount] = {};
    bool tap_ = false;
    float tap_x_ = 0, tap_y_ = 0, drag_ = 0;

    Finger* find(int64_t id);
    int control_at(float x, float y) const;   // -2 = D-pad, 0..BtnCount-1 = a button, -1 = nothing
    bool button_held(int b) const;
    uint16_t dpad_bits(float x, float y) const;
};
