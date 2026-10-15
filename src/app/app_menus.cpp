// Pause menu, options pages, cheats, level select.
#include "app.h"
#include <cstdio>

namespace {
std::string pct(int v) { return std::to_string(v) + "%"; }
std::string signed_val(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }
}

void App::open_pause_menu() {
    if (!session_) return;
    GameModule* m = session_->module();
    MenuPage p;
    p.title = m->title();
    p.subtitle = "Paused";
    p.items.push_back(MenuItem::action("Resume", [this]() { menu_.close(); }));
    p.items.push_back(MenuItem::header("Save states"));
    p.items.push_back(MenuItem::choice("Slot", {"1", "2", "3", "4", "5"}, [this]() { return state_slot_ - 1; },
                                       [this](int v) { state_slot_ = v + 1; }, "Choose the save-state slot (F5 saves, F9 loads in game)."));
    p.items.push_back(MenuItem::action("Save State", [this]() { quick_save(); menu_.close(); }));
    p.items.push_back(MenuItem::action("Load State", [this]() {
        FILE* f = open_file(save_path(state_slot_), "rb");
        if (!f) { menu_.toast("Slot " + std::to_string(state_slot_) + " is empty"); return; }
        fclose(f);
        quick_load();
        menu_.close();
    }));
    p.items.push_back(MenuItem::header("Game"));
    if (!m->levels().empty())
        p.items.push_back(MenuItem::action("Level Select", [this]() { menu_.push(level_page()); }, "Jump straight to any level."));
    if (!m->cheats().empty())
        p.items.push_back(MenuItem::action("Cheats", [this]() { menu_.push(cheats_page()); }, "Toggle cheats. They apply immediately."));
    if (!m->sounds().empty()) {
        p.items.push_back(MenuItem::action("Sound Test", [this]() { open_jukebox(current_, true); },
                                           "Play any music track or sound effect from the game."));
        p.items.push_back(MenuItem::action("Music Player", [this]() { open_jukebox(current_, false); },
                                           "Listen to the soundtrack."));
    }
    if (m->max_players() > 1)
        p.items.push_back(MenuItem::action("Player 2 Controls", [this]() { menu_.push(controls_page(1)); },
                                           "Set up the second player's keyboard keys or gamepad."));
    p.items.push_back(MenuItem::action("Options", [this]() { open_options_menu(false); }));
    p.items.push_back(MenuItem::action("Credits", [this]() { open_credits(false); }));
    p.items.push_back(MenuItem::header("System"));
    p.items.push_back(MenuItem::action("Reset Game", [this]() {
        menu_.confirm("Reset the game?", [this]() { session_->power_on(); rewind_.clear(); menu_.close(); }, "Unsaved progress will be lost.");
    }));
    p.items.push_back(MenuItem::action("Return to Launcher", [this]() {
        menu_.confirm("Return to the launcher?", [this]() { back_to_launcher(); }, "Unsaved progress will be lost.");
    }));
    p.items.push_back(MenuItem::action("Quit to Desktop", [this]() {
        menu_.confirm("Quit to desktop?", [this]() { running_ = false; }, "Unsaved progress will be lost.");
    }));
    menu_.open(std::move(p));
}

void App::open_options_menu(bool from_launcher) {
    MenuPage p;
    p.title = "Options";
    p.items.push_back(MenuItem::action("Video", [this]() { menu_.push(video_page()); }, "Display mode, widescreen, scaling and filters."));
    p.items.push_back(MenuItem::action("Colour", [this]() { menu_.push(color_page()); }, "Colour profiles and picture adjustments."));
    p.items.push_back(MenuItem::action("Audio", [this]() { menu_.push(audio_page()); }, "Volume and sound-chip options."));
    // Player 2 only matters for World of Illusion: offer it from the launcher and inside 2-player games.
    bool two_player = from_launcher || (session_ && session_->module()->max_players() > 1);
    p.items.push_back(MenuItem::action(two_player ? "Controls - Player 1" : "Controls", [this]() { menu_.push(controls_page(0)); },
                                       "Keyboard and gamepad bindings."));
    if (two_player)
        p.items.push_back(MenuItem::action(from_launcher ? "Controls - Player 2 (World of Illusion)" : "Controls - Player 2",
                                           [this]() { menu_.push(controls_page(1)); },
                                           "Second player's keys and gamepad (World of Illusion is the only 2-player game)."));
    p.items.push_back(MenuItem::header("Speed"));
    p.items.push_back(MenuItem::toggle("Fast Forward", [this]() { return settings_.fast_forward; },
                                       [this](bool v) { settings_.fast_forward = v; },
                                       "Hold Tab or the right trigger to run the game at 4x speed."));
    p.items.push_back(MenuItem::toggle("Rewind", [this]() { return settings_.rewind; },
                                       [this](bool v) { settings_.rewind = v; if (!v) rewind_.clear(); },
                                       "Hold R or the left trigger to step back through the last minute of play."));
#ifdef __ANDROID__
    p.items.push_back(MenuItem::header("Touch screen"));
    p.items.push_back(MenuItem::choice("On-screen Controls", {"When touched", "Always", "Off"}, [this]() { return settings_.touch_mode; },
                                       [this](int v) { settings_.touch_mode = v; touch_.visible = touch_active_ ? v != 2 : v == 1; },
                                       "When touched: the pad shows while you use the screen and hides when a controller is used."));
    p.items.push_back(MenuItem::slider("Controls Opacity", 10, 100, 5, [this]() { return settings_.touch_opacity; },
                                       [this](int v) { settings_.touch_opacity = v; }, [](int v) { return std::to_string(v) + "%"; }));
#endif
#ifdef _WIN32   // (keyboard hotkeys: a phone rarely has a keyboard)
    p.items.push_back(MenuItem::header("Hotkeys"));
    p.items.push_back(MenuItem::info("Menu", []() { return std::string("Esc / (Back)"); }));
    p.items.push_back(MenuItem::info("Fullscreen", []() { return std::string("F11 / Alt+Enter"); }));
    p.items.push_back(MenuItem::info("Quick save / load", []() { return std::string("F5 / F9"); }));
    p.items.push_back(MenuItem::info("Fast forward (hold)", []() { return std::string("Tab / RT"); }));
    p.items.push_back(MenuItem::info("Rewind (hold)", []() { return std::string("R / LT"); }));
    p.items.push_back(MenuItem::info("Screenshot", []() { return std::string("F12"); }));
#endif
    p.on_back = [this]() { save_settings(); };
    if (from_launcher) menu_.open(std::move(p));
    else menu_.push(std::move(p));
}

void App::open_credits(bool from_launcher) {
    MenuPage p;
    p.title = "Credits";
    p.subtitle = std::string("Mickey & Donald Collection  \xE2\x80\xA2  v") + kAppVersion;
    p.wide = true;
    p.items.push_back(MenuItem::header("Find me over at:"));
    auto link = [this](const char* label, const char* shown, const char* url) {
        MenuItem it = MenuItem::action(label, [this, url]() {
            if (autotest_) return;   // scripted test runs never open a browser
            open_url(url);
        }, "Enter / (A) opens it in your web browser.");
        it.value_text = [shown]() { return std::string(shown); };
        return it;
    };
    p.items.push_back(link("YouTube", "https://www.youtube.com/adhgaming", "https://www.youtube.com/adhgaming"));
    p.items.push_back(link("Reddit", "u/Darhkwing", "https://www.reddit.com/user/Darhkwing"));
    p.items.push_back(link("GitHub", "https://github.com/Darhkfox", "https://github.com/Darhkfox"));
    if (from_launcher) menu_.open(std::move(p));
    else menu_.push(std::move(p));   // from the pause menu: Back returns to it
}

MenuPage App::video_page() {
    MenuPage p;
    p.title = "Video";
    auto changed = [this]() { apply_settings(); };
#ifdef _WIN32
    p.items.push_back(MenuItem::choice("Display Mode", {"Windowed", "Fullscreen"}, [this]() { return settings_.fullscreen ? 1 : 0; },
                                       [this, changed](int v) { settings_.fullscreen = v == 1; changed(); }));
    {
        std::vector<std::string> labels;
        for (const MonitorInfo& m : enum_monitors()) labels.push_back(m.label);
        p.items.push_back(MenuItem::choice("Monitor", labels, [this]() {
            std::vector<MonitorInfo> l = enum_monitors();
            HMONITOR cur = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
            for (int i = 0; i < (int)l.size(); i++) if (l[i].handle == cur) return i;
            return 0;
        }, [this](int v) { move_to_monitor(v); save_settings(); },
        "Which display the game uses. You can also drag the window to another screen."));
    }
    MenuItem ws = MenuItem::slider("Window Size", 1, 6, 1, [this]() { return settings_.window_scale; },
                                   [this](int v) { settings_.window_scale = v; resize_window_to_scale(); },
                                   [](int v) { return std::to_string(v) + "x"; }, "Size of the window in windowed mode.");
    ws.enabled = [this]() { return !settings_.fullscreen; };
    p.items.push_back(ws);
#endif
    {
        // In a game only the widths it supports are offered (the setting is shared: a wider one chosen in
        // another game shows here as this game's widest).
        int top = (int)WideMode::Count - 1;
        if (session_)
            while (top > 0 && wide_ext((WideMode)top) > session_->module()->max_ext()) top--;
        std::vector<std::string> labels;
        for (int i = 0; i <= top; i++) labels.push_back(wide_mode_name((WideMode)i));
        p.items.push_back(MenuItem::choice("Widescreen", labels, [this, top]() { return std::min((int)settings_.widescreen, top); },
                                           [this, changed](int v) { settings_.widescreen = (WideMode)v; changed(); resize_window_to_scale(); },
                                           session_ ? "Shows more of each level. Menus stay 4:3."
                                                    : "Shows more of each level. Menus stay 4:3. World of Illusion goes up to 2:1."));
    }
    MenuItem hud = MenuItem::toggle("Re-anchor HUD", [this]() { return settings_.hud_anchor; },
                                    [this, changed](bool v) { settings_.hud_anchor = v; changed(); },
                                    "In widescreen, moves the status bar items out to the screen edges.");
    hud.enabled = [this]() { return settings_.widescreen != WideMode::Off; };
    p.items.push_back(hud);
    p.items.push_back(MenuItem::choice("Pixel Aspect", {"Square pixels", "CRT (4:3 pixels)", "Stretch to window"},
                                       [this]() { return (int)settings_.aspect; }, [this](int v) { settings_.aspect = (AspectMode)v; }));
    p.items.push_back(MenuItem::toggle("Integer Scaling", [this]() { return settings_.integer_scale; },
                                       [this](bool v) { settings_.integer_scale = v; }, "Scale only by whole numbers for perfectly even pixels."));
    p.items.push_back(MenuItem::choice("Filter", {"Nearest (sharp)", "Sharp bilinear", "Bilinear (smooth)", "Scanlines", "CRT"},
                                       [this]() { return settings_.filter; }, [this](int v) { settings_.filter = v; }));
#ifdef _WIN32
    p.items.push_back(MenuItem::toggle("VSync", [this]() { return settings_.vsync; }, [this, changed](bool v) { settings_.vsync = v; changed(); }));
#endif
    p.items.push_back(MenuItem::toggle("Remove Sprite Flicker", [this]() { return settings_.no_sprite_limit; },
                                       [this, changed](bool v) { settings_.no_sprite_limit = v; changed(); },
                                       "Lifts the hardware limit on sprites per line."));
    p.items.push_back(MenuItem::toggle("Show FPS", [this]() { return settings_.show_fps; }, [this](bool v) { settings_.show_fps = v; }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::color_page() {
    MenuPage p;
    p.title = "Colour";
    auto changed = [this]() { apply_settings(); };
    std::vector<std::string> modes;
    for (int i = 0; i < (int)ColorMode::Count; i++) modes.push_back(color_mode_name((ColorMode)i));
    p.items.push_back(MenuItem::choice("Colour Profile", modes, [this]() { return (int)settings_.color; },
                                       [this, changed](int v) { settings_.color = (ColorMode)v; changed(); },
                                       "Accurate uses measured Mega Drive output levels."));
    p.items.push_back(MenuItem::slider("Brightness", -50, 50, 5, [this]() { return settings_.brightness; },
                                       [this, changed](int v) { settings_.brightness = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::slider("Saturation", -100, 100, 10, [this]() { return settings_.saturation; },
                                       [this, changed](int v) { settings_.saturation = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::slider("Gamma", -50, 50, 5, [this]() { return settings_.gamma; },
                                       [this, changed](int v) { settings_.gamma = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::action("Reset Adjustments", [this, changed]() {
        settings_.brightness = settings_.saturation = settings_.gamma = 0;
        changed();
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::audio_page() {
    MenuPage p;
    p.title = "Audio";
    auto changed = [this]() { apply_settings(); };
    p.items.push_back(MenuItem::slider("Volume", 0, 100, 5, [this]() { return settings_.volume; },
                                       [this, changed](int v) { settings_.volume = v; changed(); }, pct));
    p.items.push_back(MenuItem::choice("Output Filter", {"Off (raw)", "Model 1 (warm)", "Model 2 (bright)"},
                                       [this]() { return (int)settings_.audio_filter; },
                                       [this, changed](int v) { settings_.audio_filter = (AudioFilter)v; changed(); },
                                       "Emulates the low-pass filter of the original console's audio output."));
    p.items.push_back(MenuItem::toggle("FM Chip Distortion", [this]() { return settings_.ladder_effect; },
                                       [this, changed](bool v) { settings_.ladder_effect = v; changed(); },
                                       "The YM2612 'ladder effect' heard on original Model 1 consoles."));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::controls_page(int player) {
    MenuPage p;
    bool two_player = !session_ || session_->module()->max_players() > 1;
    p.title = player == 1 ? "Controls - Player 2" : two_player ? "Controls - Player 1" : "Controls";
    p.wide = true;
    p.items.push_back(MenuItem::header("Keyboard"));
    for (int a = 0; a < ACT_COUNT; a++) {
        MenuItem it;
        it.kind = MenuItem::Bind;
        it.label = std::string(action_name(a));
        it.value_text = [this, player, a]() { int vk = settings_.keys[player][a]; return vk ? Input::key_name(vk) : std::string("-"); };
        it.on_bind = [this, player, a](int vk, uint32_t) {
            // A key can only do one thing: if another action (either player) has it, they swap.
            for (int q = 0; q < 2; q++)
                for (int b = 0; b < ACT_COUNT; b++)
                    if (settings_.keys[q][b] == vk && (q != player || b != a)) settings_.keys[q][b] = settings_.keys[player][a];
            settings_.keys[player][a] = vk;
        };
        it.hint = "Press Enter, then the new key (a key already in use swaps). Esc cancels.";
        p.items.push_back(it);
    }
    p.items.push_back(MenuItem::header("Gamepad"));
    // Name the controllers connected now, so a player can tell the slots apart.
    std::vector<std::string> pads = {player == 0 ? "Auto (first free pad)" : "Auto (next free pad)"};
    for (int i = 0; i < Input::kMaxPads; i++) {
        const PadState& ps = input_.pad(i);
        pads.push_back("Slot " + std::to_string(i + 1) + (ps.connected ? " - " + ps.name : std::string(" (empty)")));
    }
    p.items.push_back(MenuItem::choice("Device", pads, [this, player]() { return settings_.pad_index[player] + 1; },
                                       [this, player](int v) { settings_.pad_index[player] = v - 1; },
                                       "Auto takes the first connected pad the other player isn't using. Slots 1-4 are Xbox-style pads, 5-8 other controllers."));
    for (int a = 0; a < ACT_COUNT; a++) {
        MenuItem it;
        it.kind = MenuItem::Bind;
        it.bind_pad = true;
        it.label = std::string(action_name(a));
        it.value_text = [this, player, a]() {
            uint32_t b = settings_.pad_buttons[player][a];
            return b ? Input::pad_button_name(b) : std::string("-");
        };
        it.on_bind = [this, player, a](int, uint32_t b) {
            for (int c = 0; c < ACT_COUNT; c++)   // swap with the action that had this button
                if (settings_.pad_buttons[player][c] == b && c != a) settings_.pad_buttons[player][c] = settings_.pad_buttons[player][a];
            settings_.pad_buttons[player][a] = b;
        };
        it.hint = "Press Enter / (A), then the new pad button. Esc cancels.";
        p.items.push_back(it);
    }
    if (player == 0)
        p.items.push_back(MenuItem::toggle("Left Stick Moves", [this]() { return settings_.stick_as_dpad; },
                                           [this](bool v) { settings_.stick_as_dpad = v; }));
    p.items.push_back(MenuItem::action("Reset to Defaults", [this, player]() {
        Settings d;
        for (int a = 0; a < ACT_COUNT; a++) {
            settings_.keys[player][a] = d.keys[player][a];
            settings_.pad_buttons[player][a] = d.pad_buttons[player][a];
        }
        settings_.pad_index[player] = -1;
        menu_.toast("Controls reset");
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::cheats_page() {
    GameModule* m = session_->module();
    MenuPage p;
    p.title = "Cheats";
    p.subtitle = m->title();
    std::string gid = m->id();
    for (const CheatEntry& c : m->cheats()) {
        std::string key = c.key;
        p.items.push_back(MenuItem::toggle(c.name, [this, gid, key]() { return settings_.cheat(gid, key); },
                                           [this, gid, key](bool v) { settings_.set_cheat(gid, key, v); apply_settings(); }, c.desc));
    }
    p.items.push_back(MenuItem::action("Disable All", [this, gid]() {
        settings_.cheats[gid].clear();
        apply_settings();
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::level_page() {
    GameModule* m = session_->module();
    MenuPage p;
    p.title = "Level Select";
    p.subtitle = m->title();
    std::vector<std::string> variants = m->level_variants();
    if (!variants.empty()) {
        if (level_variant_ >= (int)variants.size()) level_variant_ = 0;
        p.items.push_back(MenuItem::choice("Play As", variants, [this]() { return level_variant_; },
                                           [this](int v) { level_variant_ = v; },
                                           "Stages that aren't on this route are greyed out."));
    }
    std::string group;
    std::vector<LevelEntry> lv = m->levels();
    for (int i = 0; i < (int)lv.size(); i++) {
        if (lv[i].group != group) { group = lv[i].group; p.items.push_back(MenuItem::header(group)); }
        std::string name = lv[i].name;
        MenuItem it = MenuItem::action(name, [this, i, name, group]() {
            menu_.confirm("Start " + name + "?", [this, i]() {
                if (session_->start_level(i, level_variant_)) { rewind_.clear(); menu_.close(); }
                else menu_.toast("Could not start that level");
                block_game_input_ = true;
            }, group + ". Current progress will be lost.");
        });
        unsigned mask = lv[i].variants;
        if (mask) it.enabled = [this, mask]() { return ((mask >> level_variant_) & 1) != 0; };
        p.items.push_back(it);
    }
    return p;
}

void App::open_jukebox(int game, bool sfx) {
    if (game < 0 || game >= kGameCount || !games_[game].rom.found) return;
    jukebox_.open(games_[game].module.get(), games_[game].rom.data, settings_, sfx);
}
