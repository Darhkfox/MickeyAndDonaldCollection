// Android input (see input.h): keyboard keys arrive from the app's SDL event loop as Win32 virtual-key codes
// (vk_from_scancode), controllers are read through SDL's GameController API in Xbox button positions.
#include "../input.h"
#include "../keys.h"
#include "sdl_keys.h"
#include <SDL.h>
#include <cstring>

struct Input::Impl {
    bool key[256] = {}, edge[256] = {}, edge_acc[256] = {};
    int last = 0, last_acc = 0;
    PadState pads[kMaxPads];
    SDL_GameController* ctl[kMaxPads] = {};
    bool rescan = true;

    void set_key(int vk, bool down) {
        if (down && !key[vk]) { edge_acc[vk] = true; last_acc = vk; }
        key[vk] = down;
    }

    void scan() {
        rescan = false;
        for (int i = 0; i < kMaxPads; i++)
            if (ctl[i] && !SDL_GameControllerGetAttached(ctl[i])) {
                SDL_GameControllerClose(ctl[i]);
                ctl[i] = nullptr;
                pads[i] = PadState();
            }
        for (int j = 0; j < SDL_NumJoysticks(); j++) {
            if (!SDL_IsGameController(j)) continue;
            SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
            bool open = false;
            for (int i = 0; i < kMaxPads; i++)
                if (ctl[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctl[i])) == id) open = true;
            if (open) continue;
            for (int i = 0; i < kMaxPads; i++)
                if (!ctl[i]) {
                    ctl[i] = SDL_GameControllerOpen(j);
                    if (ctl[i]) {
                        pads[i] = PadState();
                        pads[i].connected = true;
                        const char* n = SDL_GameControllerName(ctl[i]);
                        pads[i].name = n ? n : "Controller";
                    }
                    break;
                }
        }
    }

    void poll_pad(int i) {
        PadState& p = pads[i];
        SDL_GameController* c = ctl[i];
        if (!c) { p = PadState(); return; }
        static const struct { SDL_GameControllerButton b; uint32_t bit; } map[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT},
            {SDL_CONTROLLER_BUTTON_A, PAD_A}, {SDL_CONTROLLER_BUTTON_B, PAD_B},
            {SDL_CONTROLLER_BUTTON_X, PAD_X}, {SDL_CONTROLLER_BUTTON_Y, PAD_Y},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_LB}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_RB},
            {SDL_CONTROLLER_BUTTON_BACK, PAD_BACK}, {SDL_CONTROLLER_BUTTON_START, PAD_START},
            {SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_LS}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_RS},
            {SDL_CONTROLLER_BUTTON_GUIDE, PAD_GUIDE},
        };
        uint32_t b = 0;
        for (auto& m : map)
            if (SDL_GameControllerGetButton(c, m.b)) b |= m.bit;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) b |= PAD_LT;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) b |= PAD_RT;
        p.lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        p.ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        if (p.ly < -0.5f) b |= PAD_LS_UP;
        if (p.ly > 0.5f) b |= PAD_LS_DOWN;
        if (p.lx < -0.5f) b |= PAD_LS_LEFT;
        if (p.lx > 0.5f) b |= PAD_LS_RIGHT;
        p.pressed = b & ~p.buttons;
        p.buttons = b;
        p.raw_pressed = p.pressed;
        p.raw_buttons = b;
        p.connected = true;
    }
};

Input::Input() : impl(new Impl) {}
Input::~Input() { shutdown(); delete impl; }

bool Input::init(NativeWindow) {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");   // buttons by position (Nintendo pads too)
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    impl->rescan = true;
    return true;
}

void Input::shutdown() {
    for (auto*& c : impl->ctl) if (c) { SDL_GameControllerClose(c); c = nullptr; }
    for (auto& p : impl->pads) p = PadState();
    on_focus_lost();
}

void Input::poll() {
    Impl& I = *impl;
    memcpy(I.edge, I.edge_acc, sizeof(I.edge));
    memset(I.edge_acc, 0, sizeof(I.edge_acc));
    I.last = I.last_acc;
    I.last_acc = 0;
    if (I.rescan) I.scan();
    for (int i = 0; i < kMaxPads; i++) I.poll_pad(i);
}

void Input::on_device_change() { impl->rescan = true; }

void Input::on_key(int vk, bool down) {
    if (vk <= 0 || vk > 255) return;
    impl->set_key(vk, down);
}

void Input::on_focus_lost() {
    memset(impl->key, 0, sizeof(impl->key));
    memset(impl->edge_acc, 0, sizeof(impl->edge_acc));
    impl->last_acc = 0;
}

bool Input::key_down(int vk) const { return vk > 0 && vk < 256 && impl->key[vk]; }
bool Input::key_pressed(int vk) const { return vk > 0 && vk < 256 && impl->edge[vk]; }
int  Input::last_key_pressed() const { return impl->last; }

const PadState& Input::pad(int index) const {
    static const PadState none;
    return (index >= 0 && index < kMaxPads) ? impl->pads[index] : none;
}

int Input::first_connected_pad() const {
    for (int i = 0; i < kMaxPads; i++) if (impl->pads[i].connected) return i;
    return -1;
}

std::string Input::key_name(int vk) {
    switch (vk) {
    case 0: return "None";
    case VK_BACK: return "Backspace";  case VK_TAB: return "Tab";        case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";     case VK_CONTROL: return "Ctrl";   case VK_MENU: return "Alt";
    case VK_ESCAPE: return "Esc";      case VK_SPACE: return "Space";    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";  case VK_END: return "End";        case VK_HOME: return "Home";
    case VK_LEFT: return "Left";       case VK_UP: return "Up";          case VK_RIGHT: return "Right";
    case VK_DOWN: return "Down";       case VK_INSERT: return "Insert";  case VK_DELETE: return "Delete";
    case VK_LSHIFT: return "Left Shift"; case VK_RSHIFT: return "Right Shift";
    case VK_LCONTROL: return "Left Ctrl"; case VK_RCONTROL: return "Right Ctrl";
    case VK_LMENU: return "Left Alt";  case VK_RMENU: return "Right Alt";
    case VK_OEM_1: return ";";  case VK_OEM_PLUS: return "=";  case VK_OEM_COMMA: return ",";  case VK_OEM_MINUS: return "-";
    case VK_OEM_PERIOD: return "."; case VK_OEM_2: return "/"; case VK_OEM_3: return "`"; case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\"; case VK_OEM_6: return "]";  case VK_OEM_7: return "'";
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9) return "Num " + std::to_string(vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, (char)vk);
    char buf[16];
    snprintf(buf, sizeof(buf), "Key 0x%02X", vk & 0xFF);
    return buf;
}

std::string Input::pad_button_name(uint32_t bit) {
    static const char* const names[24] = {
        "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right", "A", "B", "X", "Y",
        "LB", "RB", "LT", "RT", "Back", "Start", "LS", "RS",
        "Guide", nullptr, nullptr, nullptr, "LS Up", "LS Down", "LS Left", "LS Right" };
    if (!bit) return "None";
    int i = 0;
    while (!(bit & (1u << i))) i++;
    return (i < 24 && names[i]) ? names[i] : "?";
}

int vk_from_scancode(int sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return VK_NUMPAD0 + 1 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_AC_BACK: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    default: return 0;
    }
}
