// XAudio2 streaming output, 48 kHz stereo int16.
#pragma once
#include <cstdint>

class Audio {
public:
    Audio();
    ~Audio();
    bool init(int sample_rate = 48000);
    void shutdown();

    // Queue interleaved stereo frames. Never blocks; if more than ~250 ms is
    // queued the excess is dropped.
    void push(const int16_t* stereo, int frames);
    // Number of frames queued but not yet played (for dynamic rate control).
    int  queued_frames() const;
    int  sample_rate() const;
    void set_paused(bool paused);
    void set_master_volume(float v);   // 0..1

private:
    struct Impl;
    Impl* impl;
};
