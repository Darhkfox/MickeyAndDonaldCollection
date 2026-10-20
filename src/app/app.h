// Application: window, main loop, launcher, in-game menus, music player.
#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../platform/video.h"
#include "../platform/audio.h"
#include "../platform/input.h"
#include "../platform/keys.h"
#include "../game/game.h"
#include "settings.h"
#include "session.h"
#include "romfind.h"
#include "ui.h"
#ifdef _WIN32
#include "monitors.h"
#endif
#include "rewind.h"
#include "achievements.h"
#ifdef __ANDROID__
#include "touch.h"
#endif

constexpr const char* kAppVersion = "1.1";   // shown in the launcher, the window title and Credits
constexpr double kEmuFps = 53693175.0 / (3420.0 * 262.0);   // 59.9227 Hz

struct GameSlot {
    std::unique_ptr<GameModule> module;
    RomMatch rom;
    std::unique_ptr<Session> preview;     // launcher attract-mode preview
};

class Jukebox {
public:
    // `packs`: the MD+ soundtracks found; the Music Player offers this game's as versions of the music, starting
    // with the one the game is set to play (Settings::soundtrack).
    bool open(GameModule* module, const std::vector<uint8_t>& rom, const Settings& s, bool sfx,
              const std::vector<SoundtrackPack>& packs);
    void close();
    bool active() const { return active_; }
    void update(const UiInput& in);
    void step();
    void mix_audio(std::vector<int16_t>& out, double adjust);
    void draw(Video& v, float time, uint32_t accent);
    float hint_fade_ = 0.0f;              // set by the app: a toast covers the key hints
    bool touch_ui = false;                // touch screens: help text for taps
    void apply_settings(const Settings& s) { if (session_) session_->apply_settings(s); }
    bool wants_close() const { return close_requested_; }
    int  game_switch() const { return switch_dir_; }
    void clear_switch() { switch_dir_ = 0; }
    GameModule* module() const { return module_; }

private:
    bool active_ = false;
    bool close_requested_ = false;
    int  switch_dir_ = 0;
    bool sfx_ = false;
    GameModule* module_ = nullptr;
    std::unique_ptr<Session> session_;
    std::vector<SoundEntry> list_;
    int sel_ = 0;
    int playing_ = -1;
    int silent_frames_ = 0;   // a sound effect counts as finished after a moment of silence
    double elapsed_ = 0;
    float scroll_ = 0;
    float vu_[10] = {};
    float peak_[10] = {};
    bool paused_ = false;
    Rect list_rect_ = {}, switch_rect_ = {};   // where the last draw put the track list / game switch, for taps
    Rect version_rect_ = {};                   // ... and the version switch
    float row_ = 0, list_y0_ = 0;
    bool free_scroll_ = false;           // touch: the list was dragged, so it doesn't follow the selection
    // Versions of the music: [0] the game's own (its sound driver), then each MD+ soundtrack for the game, with the
    // CD track it plays for each tune (music ID -> track, looping).
    struct Version {
        SoundtrackPack pack;
        std::map<int, std::pair<int, bool>> tracks;
        std::vector<SoundEntry> more;    // its tracks no tune names (id = -track), listed after the game's tunes
    };
    std::vector<Version> versions_;
    std::vector<SoundEntry> tunes_;      // the game's own list (list_ adds the version's `more`)
    int ver_ = 0;
    bool cd_ = false;                    // the playing entry is a soundtrack track (not the sound driver)
    double cd_len_ = 0;                  // its length (s)
    float cd_level_[2] = {};             // its loudness in the last audio mixed (left, right)
    void start(int i);                   // plays list_[i] in the current version
    void stop_playing();
    void set_version(int v);
    bool has_version(int i) const;       // list_[i] has a track in the current (MD+) version
};

class App {
public:
#ifdef _WIN32
    int run(HINSTANCE inst, int show);
    LRESULT wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
#else
    int run();
#endif

private:
    enum class Mode { Launcher, Playing };

    // platform
#ifdef _WIN32
    HWND hwnd_ = nullptr;
    WINDOWPLACEMENT windowed_placement_ = {sizeof(WINDOWPLACEMENT)};
#else
    struct SDL_Window* window_ = nullptr;
#endif
    Video video_;
    Audio audio_;
    Input input_;
    bool running_ = true;
    bool active_window_ = true;
    bool is_fullscreen_ = false;

    // data
    Settings settings_;
    std::wstring settings_path_;
    GameSlot games_[kGameCount];
    Mode mode_ = Mode::Launcher;
    int  current_ = -1;                  // index into games_ while playing
    std::unique_ptr<Session> session_;
    Menu menu_;
    Jukebox jukebox_;
    int  launcher_sel_ = 0;              // 0..kGameCount-1 games, then Options, Music Player, Credits, Quit
    int  launcher_last_card_ = 0;
    float launcher_anim_[kGameCount + 5] = {};
    Rect launcher_hit_[kGameCount + 5] = {};   // cards then buttons, where the last draw put them (taps)
#ifdef __ANDROID__
    TouchControls touch_;
    bool touch_active_ = true;           // the last input came from the touch screen (else a controller / keys)
    bool picking_roms_ = false;
    float refresh_dt_[48] = {};          // recent display frame intervals: refresh_rate() is measured
    int refresh_n_ = 0;
    void set_frame_rate();               // asks Android for a display rate that suits 60 fps
    uint64_t work_end_ = 0;              // SDL counter when the last frame's drawing was done (perf log)
    void pick_roms();                    // opens Android's file picker; the files are copied into files/roms
    void poll_pick();                    // while the picker is open: notices it closing
    bool picking_soundtrack_ = false;
    double soundtrack_note_at_ = 0;      // when the next "copying" message is due
    void pick_soundtrack();              // Android: picks an MD+ pack's folder; it is copied into files/soundtracks
    void poll_soundtrack();              // while it is picked / copied: progress, then the pack it found
#endif
    uint64_t rom_sig_ = 0;
    double rom_poll_ = 0;
    int  state_slot_ = 1;
    int  level_variant_ = 0;
    bool block_game_input_ = false;
    bool fast_forward_ = false;
    bool rewinding_ = false;
    RewindBuffer rewind_;                // recent states for rewind (one every 2 frames)
    int  rewind_tick_ = 0;
    std::vector<uint8_t> rewind_state_;
    double rewind_acc_ = 0;             // rewind steps due (real-time pacing)
    unsigned ff_audio_tick_ = 0;        // fast forward keeps every 4th game frame's audio

    // achievements (app_achievements.cpp)
    AchievementStore ach_;
    AchievementCtx ach_ctx_;             // the session's run: scratch and run facts for the module's checks
    struct AchBanner { std::string name, desc; float t = 0; };
    std::vector<AchBanner> ach_banners_; // "achievement unlocked" pop-ups, shown one after another
    float ach_locked_note_ = 0;          // cooldown of the "off while achievements are on" message
    void ach_startup();                  // loads achievements.ini; the one-time explanation
    void ach_welcome();                  // (that explanation)
    void ach_new_run(bool counts);       // power-on (counts: achievements are on) / level select (doesn't)
    void ach_frame();                    // after every emulated frame of the game being played
    void ach_award(const std::string& id, const std::string& name, const std::string& desc);
    void ach_check_collection();
    void draw_ach_banners(float dt);
    std::string ach_state_tag() const;   // the run, stored with save states
    void ach_load_tag(const std::string& tag);
    void set_achievements(bool on);
    MenuPage achievements_page(int game);   // a game's list (index into games_) and the collection's; -1 = all
    MenuItem achievements_switch();      // the on / off switch (Options, and the top of the Achievements page)

    // timing
    double time_ = 0;
    double frame_acc_ = 0;
    unsigned vsync_phase_ = 0;           // display refreshes counted, for one game frame every N of them
    bool black_frame_ = false;           // this refresh is one of black frame insertion's black ones
    double fps_display_ = 0;
    int    fps_frames_ = 0;
    double fps_timer_ = 0;
    std::vector<int16_t> audio_buf_;

    // UI input repeat state
    float rep_t_[4] = {};
    bool  rep_held_[4] = {};

    // --autotest <dir> <script>: hidden window, scripted key presses, screenshots of the output.
    // action: 0 up, 1 down, 2 shot, 3 quit, 4 realtime, 5 mark, 6 record, 7 stop recording, 8 load state,
    // 9 replay pad input (trailer gameplay found by tools/trailer/bot.py), 10 set a video setting, 11 award an achievement
    struct ScriptStep { int frame; int vk; int action; std::string arg; };
    bool autotest_ = false;
    double test_refresh_ = 0;            // autotest "set refresh=120": the display rate to assume (Windows)
    bool autotest_realtime_ = false;            // script "realtime 1": real elapsed time instead of 1/60 s per loop
    uint64_t stat_runs_ = 0, stat_pops_ = 0;   // emulated frames run / rewind states popped (autotest marks)
    // --capture-size WxH (with --autotest): fixed client size; "record <name>" writes every displayed frame and
    // its audio until "stoprec" - trailer footage. Frames go to ffmpeg (hidden, lossless H.264, <name>.mkv) when
    // it is on the PATH, else raw BGRA to <name>.vid; audio is 48 kHz stereo s16 in <name>.pcm.
    int capture_w_ = 0, capture_h_ = 0;
    bool recording_ = false;
#ifdef _WIN32
    FILE* rec_vid_ = nullptr;
    FILE* rec_pcm_ = nullptr;
    HANDLE rec_pipe_ = nullptr, rec_proc_ = nullptr;
    int rec_w_ = 0, rec_h_ = 0, rec_frames_ = 0;
    uint64_t rec_samples_ = 0;
    std::wstring rec_name_;
    bool start_ffmpeg(int w, int h);
#endif
    void start_recording(const std::string& name);
    void record_frame();
    std::vector<uint32_t> pad_stream_;          // "pads": p1 | p2 << 16 per emulated frame
    size_t pad_pos_ = 0;
    void stop_recording();
    std::wstring autotest_dir_;
    std::vector<ScriptStep> script_;
    size_t script_pos_ = 0;
    int loop_frame_ = 0;
    std::string pending_shot_;
    bool load_script(const std::wstring& path);
    void run_script();

    // per platform (app_win.cpp / app_android.cpp)
#ifdef _WIN32
    bool create_window(HINSTANCE inst, int show);
    void remember_monitor();
    static RECT centered_rect(const RECT& work, int w, int h);
#endif
    void set_fullscreen(bool fs);
    void resize_window_to_scale();
    void move_to_monitor(int index);
    double refresh_rate();
    std::string black_frames_note();     // Video menu: the refresh rate in use, or why black frames can't be shown
    void apply_settings();
    void save_settings();

    UiInput poll_ui(float dt);
    uint16_t pad_state(int player);
    bool any_game_button_held();
    bool hold_hotkey(int vk, uint32_t pad_button);
    bool hotkey_pressed(int h);               // a press hotkey (menu, quick save / load, screenshot) this frame
    void hotkey_free(int vk, uint32_t pad_button, bool& key_free, bool& pad_free) const;
    std::string hotkey_text(int h) const;     // a hotkey's key and pad button, for menus ("Tab / RT")
    void bind_hotkey_key(int h, int vk);           // Controls: a hotkey's new key / pad button (an action or the other
    void bind_hotkey_pad(int h, uint32_t button);  // hotkey that had it gets the old one)
    void rewind_step(double dt);

    void frame(double dt);
    void update_launcher(const UiInput& ui, double dt);
    void draw_launcher();
    void launch(int game);
    void find_game_roms();                // searches for the ROMs still missing; opens previews for new ones
    void poll_roms(double dt);            // launcher: notices ROMs added while it is open
    // MD+ soundtracks (soundtrack.h): the packs found, and the one the running game was started with
    std::vector<SoundtrackPack> soundtracks_;
    std::string playing_soundtrack_;
    void scan_soundtracks();
    const SoundtrackPack* soundtrack_for(int game);   // the pack the settings choose for a game, if it's there
    void reset_game();                    // Reset Game: power-on, or a fresh start when the soundtrack changed
    void back_to_launcher();
    void run_game_frames(int count, bool audio);
    void draw_game_view(int slot, Session& s, Rect area, float alpha, float dim);
    Rect game_rect(int fw, int fh, Rect area);

    // menus (app_menus.cpp)
    void open_pause_menu();
    void open_options_menu(bool from_launcher);
    void open_credits(bool from_launcher);
    MenuPage video_page();
    MenuPage color_page();
    MenuPage audio_page();
    MenuPage controls_page(int player);
    MenuPage cheats_page();
    MenuPage level_page();
    void open_jukebox(int game, bool sfx);
    void quick_save();
    void quick_load();
    void screenshot();
    std::wstring save_path(int slot);
    uint32_t accent() const;
};
