#include "settings.h"
#include "../platform/input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>

static const char* kActionKeys[ACT_COUNT] = {"up", "down", "left", "right", "a", "b", "c", "start"};

Settings::Settings() { reset_keys(); }

void Settings::reset_keys() {
    const int p1[ACT_COUNT] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'C', VK_RETURN};
    const int p2[ACT_COUNT] = {'I', 'K', 'J', 'L', 'U', 'O', 'P', VK_BACK};
    // Genesis-classics style layout: X = A, A = B, B = C.
    const uint32_t pad[ACT_COUNT] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_X, PAD_A, PAD_B, PAD_START};
    for (int i = 0; i < ACT_COUNT; i++) {
        keys[0][i] = p1[i];
        keys[1][i] = p2[i];
        pad_buttons[0][i] = pad[i];
        pad_buttons[1][i] = pad[i];
    }
}

int resolve_player_pad(const int pad_index[2], const bool* connected, int slots, int player) {
    if (pad_index[player] >= 0) return pad_index[player];
    // Player 1's Auto has priority: player 2's Auto skips whatever player 1 resolves to.
    int taken = pad_index[player ^ 1];
    if (player == 1 && taken < 0) taken = resolve_player_pad(pad_index, connected, slots, 0);
    for (int i = 0; i < slots; i++)
        if (connected[i] && i != taken) return i;
    return -1;
}

int wide_ext(WideMode m) {
    switch (m) {
    case WideMode::W16x10: return 20;
    case WideMode::W16x9:  return 40;
    case WideMode::W2x1:   return 64;
    case WideMode::W21x9:  return 108;   // 536 x 224 = 2.39:1 (3440x1440, 2560x1080 screens)
    default: return 0;
    }
}

const char* color_mode_name(ColorMode m) {
    static const char* n[] = {"Accurate", "Linear", "Vivid", "Warm CRT", "Cool", "Grayscale", "Sepia", "Handheld LCD"};
    int i = (int)m;
    return i >= 0 && i < (int)ColorMode::Count ? n[i] : "?";
}
const char* wide_mode_name(WideMode m) {
    static const char* n[] = {"Off (4:3)", "16:10", "16:9", "2:1", "21:9 (ultrawide)"};
    int i = (int)m;
    return i >= 0 && i < (int)WideMode::Count ? n[i] : "?";
}
const char* aspect_mode_name(AspectMode m) {
    static const char* n[] = {"Square pixels", "CRT (4:3 pixels)", "Stretch to window"};
    int i = (int)m;
    return i >= 0 && i < (int)AspectMode::Count ? n[i] : "?";
}
const char* audio_filter_name(AudioFilter f) {
    static const char* n[] = {"Off (raw)", "Model 1 (warm)", "Model 2 (bright)"};
    int i = (int)f;
    return i >= 0 && i < (int)AudioFilter::Count ? n[i] : "?";
}
const char* action_name(int act) {
    static const char* n[] = {"Up", "Down", "Left", "Right", "A", "B", "C", "Start"};
    return act >= 0 && act < ACT_COUNT ? n[act] : "?";
}

bool Settings::cheat(const std::string& game, const std::string& key) const {
    auto g = cheats.find(game);
    if (g == cheats.end()) return false;
    auto c = g->second.find(key);
    return c != g->second.end() && c->second;
}
void Settings::set_cheat(const std::string& game, const std::string& key, bool on) { cheats[game][key] = on; }

// ---------------------------------------------------------------------------
// INI I/O
// ---------------------------------------------------------------------------
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}

bool Settings::load(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = trim(line.substr(1, line.find(']') - 1)); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        int iv = atoi(v.c_str());
        if (section == "video") {
            if (k == "fullscreen") fullscreen = iv != 0;
            else if (k == "monitor") monitor = v;
            else if (k == "window_scale") window_scale = clampi(iv, 1, 8);
            else if (k == "vsync") vsync = iv != 0;
            else if (k == "filter") filter = clampi(iv, 0, 4);
            else if (k == "widescreen") widescreen = (WideMode)clampi(iv, 0, (int)WideMode::Count - 1);
            else if (k == "aspect") aspect = (AspectMode)clampi(iv, 0, (int)AspectMode::Count - 1);
            else if (k == "integer_scale") integer_scale = iv != 0;
            else if (k == "color") color = (ColorMode)clampi(iv, 0, (int)ColorMode::Count - 1);
            else if (k == "brightness") brightness = clampi(iv, -50, 50);
            else if (k == "saturation") saturation = clampi(iv, -100, 100);
            else if (k == "gamma") gamma = clampi(iv, -50, 50);
            else if (k == "no_sprite_limit") no_sprite_limit = iv != 0;
            else if (k == "hud_anchor") hud_anchor = iv != 0;
            else if (k == "show_fps") show_fps = iv != 0;
        } else if (section == "audio") {
            if (k == "volume") volume = clampi(iv, 0, 100);
            else if (k == "music_volume") music_volume = clampi(iv, 0, 100);
            else if (k == "filter") audio_filter = (AudioFilter)clampi(iv, 0, (int)AudioFilter::Count - 1);
            else if (k == "ladder_effect") ladder_effect = iv != 0;
        } else if (section == "input1" || section == "input2") {
            int p = section == "input1" ? 0 : 1;
            for (int a = 0; a < ACT_COUNT; a++) {
                if (k == std::string("key_") + kActionKeys[a]) keys[p][a] = iv;
                else if (k == std::string("pad_") + kActionKeys[a]) pad_buttons[p][a] = (uint32_t)strtoul(v.c_str(), nullptr, 0);
            }
            if (k == "pad_index") pad_index[p] = clampi(iv, -1, Input::kMaxPads - 1);
            else if (k == "stick_as_dpad" && p == 0) stick_as_dpad = iv != 0;
        } else if (section.rfind("cheats.", 0) == 0) {
            cheats[section.substr(7)][k] = iv != 0;
        } else if (section == "general") {
            if (k == "last_game") last_game = v;
            else if (k == "fast_forward") fast_forward = iv != 0;
            else if (k == "rewind") rewind = iv != 0;
        }
    }
    return true;
}

bool Settings::save(const std::wstring& path) const {
    std::ostringstream o;
    o << "; Disney Classics - settings\n\n[video]\n"
      << "fullscreen=" << fullscreen << "\nmonitor=" << monitor << "\nwindow_scale=" << window_scale << "\nvsync=" << vsync
      << "\nfilter=" << filter << "\nwidescreen=" << (int)widescreen << "\naspect=" << (int)aspect
      << "\ninteger_scale=" << integer_scale << "\ncolor=" << (int)color << "\nbrightness=" << brightness
      << "\nsaturation=" << saturation << "\ngamma=" << gamma << "\nno_sprite_limit=" << no_sprite_limit
      << "\nhud_anchor=" << hud_anchor << "\nshow_fps=" << show_fps << "\n\n[audio]\n"
      << "volume=" << volume << "\nmusic_volume=" << music_volume << "\nfilter=" << (int)audio_filter
      << "\nladder_effect=" << ladder_effect << "\n";
    for (int p = 0; p < 2; p++) {
        o << "\n[input" << (p + 1) << "]\n";
        for (int a = 0; a < ACT_COUNT; a++) o << "key_" << kActionKeys[a] << "=" << keys[p][a] << "\n";
        for (int a = 0; a < ACT_COUNT; a++) o << "pad_" << kActionKeys[a] << "=0x" << std::hex << pad_buttons[p][a] << std::dec << "\n";
        o << "pad_index=" << pad_index[p] << "\n";
        if (p == 0) o << "stick_as_dpad=" << stick_as_dpad << "\n";
    }
    for (auto& g : cheats) {
        o << "\n[cheats." << g.first << "]\n";
        for (auto& c : g.second) o << c.first << "=" << c.second << "\n";
    }
    o << "\n[general]\nlast_game=" << last_game << "\nfast_forward=" << fast_forward << "\nrewind=" << rewind << "\n";
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << o.str();
    return true;
}
