// Keyboard (Win32 messages / GetAsyncKeyState) + XInput + DirectInput8 game controllers.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "native.h"

// Normalised controller buttons (Xbox layout). DirectInput devices are mapped
// onto this layout with a default mapping (see input.cpp), raw buttons are
// still available via raw_buttons for rebinding.
enum PadButton : uint32_t {
    PAD_UP = 1u << 0, PAD_DOWN = 1u << 1, PAD_LEFT = 1u << 2, PAD_RIGHT = 1u << 3,
    PAD_A = 1u << 4, PAD_B = 1u << 5, PAD_X = 1u << 6, PAD_Y = 1u << 7,
    PAD_LB = 1u << 8, PAD_RB = 1u << 9, PAD_LT = 1u << 10, PAD_RT = 1u << 11,
    PAD_BACK = 1u << 12, PAD_START = 1u << 13, PAD_LS = 1u << 14, PAD_RS = 1u << 15,
    PAD_GUIDE = 1u << 16,
    // Left stick as digital directions (with deadzone):
    PAD_LS_UP = 1u << 20, PAD_LS_DOWN = 1u << 21, PAD_LS_LEFT = 1u << 22, PAD_LS_RIGHT = 1u << 23,
};

struct PadState {
    bool     connected = false;
    bool     is_xinput = false;
    std::string name;
    uint32_t buttons = 0;          // PadButton bits (normalised)
    uint32_t pressed = 0;          // buttons that went down this poll
    float    lx = 0, ly = 0;       // left stick -1..1 (y up = -1)
    uint64_t raw_buttons = 0;      // DirectInput raw button bits (0..63), XInput: same as buttons
    uint64_t raw_pressed = 0;
};

class Input {
public:
    Input();
    ~Input();
    bool init(NativeWindow window);
    void shutdown();

    // Call once per frame before reading state. Handles hot-plug (XInput every
    // poll, DirectInput re-enumerated on WM_DEVICECHANGE via on_device_change()).
    void poll();
    void on_device_change();

    // Feed from WndProc so key state is tracked only while focused.
    void on_key(int vk, bool down);
    void on_focus_lost();                 // releases all keys

    bool key_down(int vk) const;          // Win32 virtual-key code
    bool key_pressed(int vk) const;       // went down since last poll()
    int  last_key_pressed() const;        // VK pressed during last poll, or 0

    static constexpr int kMaxPads = 8;
    int  pad_count() const { return kMaxPads; }
    const PadState& pad(int index) const;
    // Index of the first connected pad (XInput slots 0-3 first, then DirectInput 4-7), or -1.
    int  first_connected_pad() const;
    // Rumble etc. is not needed.

    static std::string key_name(int vk);
    static std::string pad_button_name(uint32_t pad_button_bit);

private:
    struct Impl;
    Impl* impl;
};
