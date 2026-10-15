// Menu system drawn with the Video overlay: stacked pages of items
// (actions, toggles, choices, sliders, key bindings), plus toast messages.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../platform/video.h"

struct UiInput {
    bool up = false, down = false, left = false, right = false;   // auto-repeating
    bool confirm = false, back = false, menu = false;
    bool page_left = false, page_right = false;
    int  key_pressed = 0;          // raw VK pressed this frame (for rebinding)
    uint32_t pad_pressed = 0;      // normalised pad buttons pressed this frame
};

struct MenuItem {
    enum Kind { Action, Toggle, Choice, Slider, Bind, Header, Info } kind = Action;
    std::string label;
    std::string hint;
    std::function<void()> on_activate;                 // Action
    std::function<bool()> get_bool;                    // Toggle
    std::function<void(bool)> set_bool;
    std::vector<std::string> choices;                  // Choice
    std::function<int()> get_int;                      // Choice / Slider
    std::function<void(int)> set_int;
    int min = 0, max = 100, step = 1;                  // Slider
    std::function<std::string(int)> format;            // Slider value text
    std::function<std::string()> value_text;           // Bind / Action / Info value on the right
    std::function<void(int vk, uint32_t pad)> on_bind; // Bind: called with the captured input
    bool bind_pad = false;                             // Bind expects a pad button (else a key)
    std::function<bool()> enabled;
    bool selectable() const { return kind != Header && kind != Info; }

    static MenuItem action(std::string label, std::function<void()> fn, std::string hint = "");
    static MenuItem toggle(std::string label, std::function<bool()> get, std::function<void(bool)> set, std::string hint = "");
    static MenuItem choice(std::string label, std::vector<std::string> choices, std::function<int()> get,
                           std::function<void(int)> set, std::string hint = "");
    static MenuItem slider(std::string label, int min, int max, int step, std::function<int()> get,
                           std::function<void(int)> set, std::function<std::string(int)> fmt, std::string hint = "");
    static MenuItem header(std::string label);
    static MenuItem info(std::string label, std::function<std::string()> value = nullptr);
};

struct MenuPage {
    std::string title;
    std::string subtitle;
    std::vector<MenuItem> items;
    int sel = -1;
    float scroll = 0;
    std::function<void()> on_back;     // called when the page is popped with Back
    std::function<void()> on_frame;    // called every frame while on top (live refresh)
    bool wide = false;                 // use a wider panel (long lists)
};

class Menu {
public:
    void open(MenuPage page);          // replaces the stack
    void push(MenuPage page);
    void pop();
    void close();
    bool active() const { return !stack_.empty(); }
    MenuPage* top() { return stack_.empty() ? nullptr : &stack_.back(); }
    size_t depth() const { return stack_.size(); }

    // Returns true if the input was consumed.
    void update(const UiInput& in);
    void draw(Video& v, float time);

    uint32_t accent = 0xFFE8B84A;
    std::string footer;                // controls help line
    bool capturing() const { return capture_; }

    void toast(const std::string& msg, float seconds = 2.2f);
    void draw_toasts(Video& v, float dt, bool bottom = false);
    // 0..1: how much a toast is showing; screens fade their bottom key hints by it (the toast sits over them)
    float toast_cover() const;

    // Simple confirmation dialog.
    void confirm(const std::string& question, std::function<void()> yes, const std::string& detail = "");

private:
    std::vector<MenuPage> stack_;
    bool capture_ = false;
    float capture_block_ = 0;
    struct Toast { std::string text; float t; float life; };
    std::vector<Toast> toasts_;
    void fix_selection(MenuPage& p);
    void move(MenuPage& p, int dir);
};

// Shared drawing helpers
void draw_panel(Video& v, Rect r, uint32_t accent, float alpha = 1.0f);
uint32_t with_alpha(uint32_t c, float a);
uint32_t lerp_color(uint32_t a, uint32_t b, float t);
