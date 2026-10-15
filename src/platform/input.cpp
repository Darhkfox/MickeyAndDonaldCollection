// Keyboard + XInput + DirectInput8 controllers (see input.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <xinput.h>
#include <dinput.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#include "input.h"

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

namespace {

std::string utf8(const wchar_t* s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, nullptr, nullptr);
    return out;
}

// Parses the hex number following "VID_"/"VID&" (Bluetooth names carry 8 digits: source + id).
bool parse_id(const std::wstring& s, const wchar_t* tag, unsigned& out) {
    for (size_t p = s.find(tag); p != std::wstring::npos; p = s.find(tag, p + 1)) {
        wchar_t sep = p + 3 < s.size() ? s[p + 3] : 0;
        if (sep != L'_' && sep != L'&') continue;
        unsigned v = 0; int digits = 0;
        for (size_t i = p + 4; i < s.size() && digits < 8 && iswxdigit(s[i]); i++, digits++)
            v = v * 16 + (unsigned)(iswdigit(s[i]) ? s[i] - L'0' : (towupper(s[i]) - L'A' + 10));
        if (digits >= 4) { out = v & 0xFFFF; return true; }
    }
    return false;
}

// VID/PID pairs (as MAKELONG(vid, pid), matching DIDEVICEINSTANCE::guidProduct.Data1) of all
// XInput-capable HID devices; their raw input device names contain "IG_".
std::vector<DWORD> xinput_product_ids() {
    std::vector<DWORD> ids;
    std::vector<RAWINPUTDEVICELIST> list;
    UINT n = 0;
    for (int tries = 0; tries < 4; tries++) {
        if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) != 0) return ids;
        list.resize(n);
        UINT got = GetRawInputDeviceList(list.data(), &n, sizeof(RAWINPUTDEVICELIST));
        if (got != (UINT)-1) { list.resize(got); break; }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return ids;
        list.clear();
    }
    for (auto& d : list) {
        if (d.dwType != RIM_TYPEHID) continue;
        UINT len = 0;
        if (GetRawInputDeviceInfoW(d.hDevice, RIDI_DEVICENAME, nullptr, &len) != 0 || len == 0) continue;
        std::wstring name(len, L'\0');
        if (GetRawInputDeviceInfoW(d.hDevice, RIDI_DEVICENAME, &name[0], &len) == (UINT)-1) continue;
        for (auto& c : name) c = (wchar_t)towupper(c);
        if (name.find(L"IG_") == std::wstring::npos) continue;
        unsigned vid = 0, pid = 0;
        if (parse_id(name, L"VID", vid) && parse_id(name, L"PID", pid))
            ids.push_back((DWORD)MAKELONG(vid, pid));
    }
    return ids;
}

const char* xinput_name(BYTE subtype) {
    switch (subtype) {
    case XINPUT_DEVSUBTYPE_WHEEL:        return "Racing Wheel";
    case XINPUT_DEVSUBTYPE_ARCADE_STICK: return "Arcade Stick";
    case XINPUT_DEVSUBTYPE_FLIGHT_STICK: return "Flight Stick";
    case XINPUT_DEVSUBTYPE_DANCE_PAD:    return "Dance Pad";
    case XINPUT_DEVSUBTYPE_GUITAR:
    case XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE:
    case XINPUT_DEVSUBTYPE_GUITAR_BASS:  return "Guitar";
    case XINPUT_DEVSUBTYPE_DRUM_KIT:     return "Drum Kit";
    case XINPUT_DEVSUBTYPE_ARCADE_PAD:   return "Arcade Pad";
    default:                             return "Xbox Controller";
    }
}

// Left stick: analog value with a radial deadzone + digital directions at ~0.5.
uint32_t stick(float x, float y, float deadzone, float& ox, float& oy) {
    x = std::max(-1.0f, std::min(x, 1.0f));
    y = std::max(-1.0f, std::min(y, 1.0f));
    uint32_t b = 0;
    if (x < -0.5f) b |= PAD_LS_LEFT;
    if (x > 0.5f)  b |= PAD_LS_RIGHT;
    if (y < -0.5f) b |= PAD_LS_UP;
    if (y > 0.5f)  b |= PAD_LS_DOWN;
    float mag = std::sqrt(x * x + y * y);
    if (mag <= deadzone) { ox = oy = 0; return b; }
    float s = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone)) / mag;
    ox = x * s; oy = y * s;
    return b;
}

const int kFirstDInputSlot = 4;
const int kDInputSlots = Input::kMaxPads - kFirstDInputSlot;
// Default DirectInput button -> normalised mapping (PlayStation-style order).
const uint32_t kDIButtonMap[13] = { PAD_X, PAD_A, PAD_B, PAD_Y, PAD_LB, PAD_RB, PAD_LT, PAD_RT,
                                    PAD_BACK, PAD_START, PAD_LS, PAD_RS, PAD_GUIDE };

} // namespace

struct Input::Impl {
    HWND hwnd = nullptr;
    bool key[256] = {}, edge_acc[256] = {}, edge[256] = {};
    int last_acc = 0, last = 0;

    PadState pads[kMaxPads];
    bool xi_connected[4] = {};
    ULONGLONG xi_next_try[4] = {};

    IDirectInput8W* di = nullptr;
    struct DIPad { IDirectInputDevice8W* dev = nullptr; GUID inst = {}; bool seen = false; };
    DIPad dpads[kDInputSlots];
    std::vector<DWORD> xinput_ids;
    bool enum_pending = false;
    ULONGLONG enum_again_at = 0;    // second enumeration after a device change (late arrivals)

    void set_key(int vk, bool down) {
        if (down && !key[vk]) { edge_acc[vk] = true; last_acc = vk; }
        key[vk] = down;
    }

    // ---- DirectInput -------------------------------------------------------------
    static BOOL CALLBACK enum_axis(LPCDIDEVICEOBJECTINSTANCEW obj, LPVOID user) {
        DIPROPRANGE r = {};
        r.diph.dwSize = sizeof(DIPROPRANGE);
        r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        r.diph.dwHow = DIPH_BYID;
        r.diph.dwObj = obj->dwType;
        r.lMin = -1000; r.lMax = 1000;
        ((IDirectInputDevice8W*)user)->SetProperty(DIPROP_RANGE, &r.diph);
        return DIENUM_CONTINUE;
    }
    static BOOL CALLBACK enum_device(LPCDIDEVICEINSTANCEW inst, LPVOID user) {
        ((Impl*)user)->add_device(inst);
        return DIENUM_CONTINUE;
    }
    void add_device(LPCDIDEVICEINSTANCEW inst) {
        for (DWORD id : xinput_ids) if (id == inst->guidProduct.Data1) return;   // handled by XInput
        int slot = -1;
        for (int i = 0; i < kDInputSlots; i++) {
            DIPad& d = dpads[i];
            if (!d.dev || d.inst != inst->guidInstance) continue;
            if (pads[kFirstDInputSlot + i].connected) { d.seen = true; return; }
            d.dev->Unacquire(); d.dev->Release(); d.dev = nullptr;   // stale after unplug: recreate in place
            slot = i;
        }
        for (int i = 0; i < kDInputSlots && slot < 0; i++) if (!dpads[i].dev) slot = i;
        if (slot < 0) return;
        IDirectInputDevice8W* dev = nullptr;
        if (FAILED(di->CreateDevice(inst->guidInstance, &dev, nullptr))) { pads[kFirstDInputSlot + slot] = PadState(); return; }
        if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(dev->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
            dev->Release(); pads[kFirstDInputSlot + slot] = PadState(); return;
        }
        dev->EnumObjects(enum_axis, dev, DIDFT_AXIS);
        dev->Acquire();
        DIPad& d = dpads[slot];
        d.dev = dev; d.inst = inst->guidInstance; d.seen = true;
        PadState& p = pads[kFirstDInputSlot + slot];
        p = PadState();
        p.name = utf8(inst->tszInstanceName);
        if (p.name.empty()) p.name = utf8(inst->tszProductName);
        p.connected = true;
    }
    void enumerate() {
        enum_pending = false;
        if (!di) return;
        xinput_ids = xinput_product_ids();
        for (auto& d : dpads) d.seen = false;
        di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_device, this, DIEDFL_ATTACHEDONLY);
        for (int i = 0; i < kDInputSlots; i++) {
            DIPad& d = dpads[i];
            if (!d.dev || d.seen) continue;
            d.dev->Unacquire(); d.dev->Release(); d.dev = nullptr;
            pads[kFirstDInputSlot + i] = PadState();
        }
    }
    void poll_dinput(int i) {
        DIPad& d = dpads[i];
        PadState& p = pads[kFirstDInputSlot + i];
        uint32_t prev = p.buttons;
        uint64_t prev_raw = p.raw_buttons;
        if (!d.dev) return;
        DIJOYSTATE2 js;
        HRESULT hr = d.dev->Poll();
        if (FAILED(hr)) { d.dev->Acquire(); d.dev->Poll(); }
        hr = d.dev->GetDeviceState(sizeof(js), &js);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            if (SUCCEEDED(d.dev->Acquire())) hr = d.dev->GetDeviceState(sizeof(js), &js);
        }
        if (FAILED(hr)) {   // unplugged or unavailable: report disconnected until re-enumerated
            p.connected = false; p.buttons = p.pressed = 0; p.raw_buttons = p.raw_pressed = 0; p.lx = p.ly = 0;
            return;
        }
        p.connected = true;
        p.is_xinput = false;
        uint64_t raw = 0;
        uint32_t b = 0;
        for (int k = 0; k < 64; k++)
            if (js.rgbButtons[k] & 0x80) {
                raw |= 1ull << k;
                if (k < 13) b |= kDIButtonMap[k];
            }
        DWORD pov = js.rgdwPOV[0];
        if (LOWORD(pov) != 0xFFFF) {
            if (pov >= 31500 || pov <= 4500) b |= PAD_UP;
            if (pov >= 4500 && pov <= 13500) b |= PAD_RIGHT;
            if (pov >= 13500 && pov <= 22500) b |= PAD_DOWN;
            if (pov >= 22500 && pov <= 31500) b |= PAD_LEFT;
        }
        b |= stick(js.lX / 1000.0f, js.lY / 1000.0f, 0.15f, p.lx, p.ly);
        p.buttons = b;
        p.pressed = b & ~prev;
        p.raw_buttons = raw;
        p.raw_pressed = raw & ~prev_raw;
    }

    // ---- XInput -------------------------------------------------------------------
    void poll_xinput(int i, ULONGLONG now) {
        PadState& p = pads[i];
        if (!xi_connected[i] && now < xi_next_try[i]) return;   // avoid the XInputGetState stall on empty slots
        XINPUT_STATE st = {};
        if (XInputGetState((DWORD)i, &st) != ERROR_SUCCESS) {
            xi_connected[i] = false;
            xi_next_try[i] = now + 1000;
            p = PadState();
            return;
        }
        if (!xi_connected[i]) {
            xi_connected[i] = true;
            XINPUT_CAPABILITIES caps = {};
            BYTE sub = XINPUT_DEVSUBTYPE_GAMEPAD;
            if (XInputGetCapabilities((DWORD)i, 0, &caps) == ERROR_SUCCESS) sub = caps.SubType;
            p = PadState();
            p.name = std::string(xinput_name(sub)) + " #" + std::to_string(i + 1);
        }
        const XINPUT_GAMEPAD& g = st.Gamepad;
        static const struct { WORD xi; uint32_t pad; } map[] = {
            { XINPUT_GAMEPAD_DPAD_UP, PAD_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, PAD_DOWN },
            { XINPUT_GAMEPAD_DPAD_LEFT, PAD_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, PAD_RIGHT },
            { XINPUT_GAMEPAD_A, PAD_A }, { XINPUT_GAMEPAD_B, PAD_B }, { XINPUT_GAMEPAD_X, PAD_X }, { XINPUT_GAMEPAD_Y, PAD_Y },
            { XINPUT_GAMEPAD_LEFT_SHOULDER, PAD_LB }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, PAD_RB },
            { XINPUT_GAMEPAD_BACK, PAD_BACK }, { XINPUT_GAMEPAD_START, PAD_START },
            { XINPUT_GAMEPAD_LEFT_THUMB, PAD_LS }, { XINPUT_GAMEPAD_RIGHT_THUMB, PAD_RS },
        };
        uint32_t b = 0;
        for (auto& m : map) if (g.wButtons & m.xi) b |= m.pad;
        if (g.bLeftTrigger > 64) b |= PAD_LT;
        if (g.bRightTrigger > 64) b |= PAD_RT;
        b |= stick(g.sThumbLX / 32767.0f, -g.sThumbLY / 32767.0f,
                   XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f, p.lx, p.ly);
        uint32_t prev = p.buttons;
        p.connected = true;
        p.is_xinput = true;
        p.buttons = b;
        p.pressed = b & ~prev;
        p.raw_buttons = b;
        p.raw_pressed = p.pressed;
    }
};

Input::Input() : impl(new Impl) {}
Input::~Input() { shutdown(); delete impl; }

bool Input::init(HWND hwnd) {
    shutdown();
    Impl& I = *impl;
    I.hwnd = hwnd;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  (void**)&I.di, nullptr)))
        I.di = nullptr;   // keyboard + XInput still work
    I.enumerate();
    return true;
}

void Input::shutdown() {
    Impl& I = *impl;
    for (auto& d : I.dpads) if (d.dev) { d.dev->Unacquire(); d.dev->Release(); d.dev = nullptr; }
    if (I.di) { I.di->Release(); I.di = nullptr; }
    for (auto& p : I.pads) p = PadState();
    for (int i = 0; i < 4; i++) { I.xi_connected[i] = false; I.xi_next_try[i] = 0; }
    on_focus_lost();
}

void Input::poll() {
    Impl& I = *impl;
    memcpy(I.edge, I.edge_acc, sizeof(I.edge));
    memset(I.edge_acc, 0, sizeof(I.edge_acc));
    I.last = I.last_acc;
    I.last_acc = 0;

    ULONGLONG now = GetTickCount64();
    if (I.enum_again_at && now >= I.enum_again_at) { I.enum_again_at = 0; I.enum_pending = true; }
    if (I.enum_pending) I.enumerate();
    for (int i = 0; i < 4; i++) I.poll_xinput(i, now);
    for (int i = 0; i < kDInputSlots; i++) I.poll_dinput(i);
}

void Input::on_device_change() {
    Impl& I = *impl;
    I.enum_pending = true;                            // coalesced; done in the next poll()
    I.enum_again_at = GetTickCount64() + 1500;
    for (auto& t : I.xi_next_try) t = 0;              // re-probe empty XInput slots right away
}

void Input::on_key(int vk, bool down) {
    Impl& I = *impl;
    if (vk <= 0 || vk > 255) return;
    if (vk == VK_SNAPSHOT && !down && !I.key[vk]) { I.edge_acc[vk] = true; I.last_acc = vk; return; }  // key-up only
    I.set_key(vk, down);
    // Keep generic and left/right modifier codes consistent whichever the WndProc passes.
    static const int sided[3][3] = { { VK_SHIFT, VK_LSHIFT, VK_RSHIFT }, { VK_CONTROL, VK_LCONTROL, VK_RCONTROL },
                                     { VK_MENU, VK_LMENU, VK_RMENU } };
    for (auto& s : sided) {
        if (vk == s[0]) {
            I.set_key(s[1], (GetKeyState(s[1]) & 0x8000) != 0);
            I.set_key(s[2], (GetKeyState(s[2]) & 0x8000) != 0);
            if (down && !I.key[s[1]] && !I.key[s[2]]) I.set_key(s[1], true);   // synthetic input
        } else if (vk == s[1] || vk == s[2]) {
            bool d = I.key[s[1]] || I.key[s[2]];
            if (d && !I.key[s[0]]) I.edge_acc[s[0]] = true;
            I.key[s[0]] = d;
        }
    }
}

void Input::on_focus_lost() {
    Impl& I = *impl;
    memset(I.key, 0, sizeof(I.key));
    memset(I.edge_acc, 0, sizeof(I.edge_acc));
    I.last_acc = 0;
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
    case VK_LBUTTON: return "Mouse Left";   case VK_RBUTTON: return "Mouse Right";
    case VK_MBUTTON: return "Mouse Middle"; case VK_XBUTTON1: return "Mouse 4"; case VK_XBUTTON2: return "Mouse 5";
    case VK_BACK: return "Backspace";  case VK_TAB: return "Tab";         case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";     case VK_CONTROL: return "Ctrl";    case VK_MENU: return "Alt";
    case VK_PAUSE: return "Pause";     case VK_CAPITAL: return "Caps Lock"; case VK_ESCAPE: return "Esc";
    case VK_SPACE: return "Space";     case VK_PRIOR: return "Page Up";   case VK_NEXT: return "Page Down";
    case VK_END: return "End";         case VK_HOME: return "Home";
    case VK_LEFT: return "Left";       case VK_UP: return "Up";           case VK_RIGHT: return "Right"; case VK_DOWN: return "Down";
    case VK_SNAPSHOT: return "Print Screen"; case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete";
    case VK_LWIN: return "Left Win";   case VK_RWIN: return "Right Win";  case VK_APPS: return "Menu";
    case VK_MULTIPLY: return "Num *";  case VK_ADD: return "Num +";       case VK_SUBTRACT: return "Num -";
    case VK_DECIMAL: return "Num .";   case VK_DIVIDE: return "Num /";    case VK_SEPARATOR: return "Num Enter";
    case VK_NUMLOCK: return "Num Lock"; case VK_SCROLL: return "Scroll Lock";
    case VK_LSHIFT: return "Left Shift"; case VK_RSHIFT: return "Right Shift";
    case VK_LCONTROL: return "Left Ctrl"; case VK_RCONTROL: return "Right Ctrl";
    case VK_LMENU: return "Left Alt";  case VK_RMENU: return "Right Alt";
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num " + std::to_string(vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    // Layout-aware name for character keys.
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc) {
        wchar_t buf[64] = {};
        if (GetKeyNameTextW((LONG)(sc << 16), buf, 64) > 0) {
            std::string s = utf8(buf);
            if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 32);
            return s;
        }
    }
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
