// Persistent user settings (settings.ini next to the executable).
#pragma once
#include <cstdint>
#include <string>
#include <map>

enum class ColorMode : int {
    Accurate = 0,   // measured Mega Drive DAC levels
    Linear,         // evenly spaced levels (brighter midtones, common in emulators)
    Vivid,          // accurate + extra saturation/contrast
    Warm,           // soft warm CRT-like tint
    Cool,           // slightly cool, crisp
    Grayscale,
    Sepia,
    Handheld,       // 4-shade green LCD look
    Count
};

enum class WideMode : int { Off = 0, W16x10, W16x9, W2x1, W21x9, Count };   // 320, 360, 400, 448, 536 px wide
enum class AspectMode : int { Square = 0, Crt4x3, Stretch, Count };
enum class AudioFilter : int { Off = 0, Model1, Model2, Count };

// Mega Drive pad actions (bindable).
enum MdAction : int { ACT_UP = 0, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_A, ACT_B, ACT_C, ACT_START, ACT_COUNT };

struct Settings {
    // Video
    bool        fullscreen = true;          // first launch opens fullscreen (F11 / Alt+Enter for a window)
    std::string monitor;                    // GDI device name of the display to use (empty = primary)
    int         window_scale = 3;
    bool        vsync = true;
    int         filter = 1;                 // ScaleFilter
    WideMode    widescreen = WideMode::W16x9;
    AspectMode  aspect = AspectMode::Square;
    bool        integer_scale = false;
    ColorMode   color = ColorMode::Accurate;
    int         brightness = 0;             // -50..50
    int         saturation = 0;             // -100..100
    int         gamma = 0;                  // -50..50
    bool        no_sprite_limit = true;     // removes flicker (sprites per line)
    bool        hud_anchor = true;          // re-anchor HUD to the screen edges in widescreen
    bool        show_fps = false;

    // Audio
    int         volume = 80;                // 0..100
    int         music_volume = 100;         // FM/PSG balance handled by chip gains
    AudioFilter audio_filter = AudioFilter::Model1;
    bool        ladder_effect = true;

    // Input: keyboard virtual-key codes and pad button bits (PadButton) per action, player 1/2
    int         keys[2][ACT_COUNT];
    uint32_t    pad_buttons[2][ACT_COUNT];
    int         pad_index[2] = {-1, -1};    // -1 = first connected pad (player 1) / second (player 2)
    bool        stick_as_dpad = true;

    // Hold-to-use speed controls (keyboard Tab / R, pad right / left trigger)
    bool        fast_forward = true;
    bool        rewind = true;

    // Touch screens (Android): on-screen pad 0 = when the screen is touched, 1 = always, 2 = never; opacity %
    int         touch_mode = 0;
    int         touch_opacity = 55;

    // Cheats per game: game id -> cheat key -> enabled
    std::map<std::string, std::map<std::string, bool>> cheats;

    std::string last_game;

    Settings();
    void reset_keys();
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    bool cheat(const std::string& game, const std::string& key) const;
    void set_cheat(const std::string& game, const std::string& key, bool on);
};

// The controller slot a player (0/1) uses, or -1: their chosen slot, or (Auto, pad_index -1) the first
// connected pad the other player isn't using - so two players never share a pad unless both picked it.
int resolve_player_pad(const int pad_index[2], const bool* connected, int slots, int player);

int wide_ext(WideMode m);           // extra pixels per side for a widescreen mode
const char* color_mode_name(ColorMode m);
const char* wide_mode_name(WideMode m);
const char* aspect_mode_name(AspectMode m);
const char* audio_filter_name(AudioFilter f);
const char* action_name(int act);
