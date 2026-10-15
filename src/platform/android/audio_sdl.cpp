// SDL audio output for Android (see audio.h): 48 kHz stereo int16 pushed into SDL's queue.
#include "../audio.h"
#include <SDL.h>
#include <algorithm>
#include <vector>

struct Audio::Impl {
    SDL_AudioDeviceID dev = 0;
    int rate = 48000;
    float volume = 1.0f;
    std::vector<int16_t> tmp;
};

Audio::Audio() : impl(new Impl) {}
Audio::~Audio() { shutdown(); delete impl; }

bool Audio::init(int sample_rate) {
    shutdown();
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want = {}, have = {};
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    impl->dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!impl->dev) return false;
    impl->rate = have.freq;
    SDL_PauseAudioDevice(impl->dev, 0);
    return true;
}

void Audio::shutdown() {
    if (impl->dev) {
        SDL_CloseAudioDevice(impl->dev);
        impl->dev = 0;
    }
}

void Audio::push(const int16_t* stereo, int frames) {
    if (!impl->dev || frames <= 0) return;
    // never more than ~250 ms queued: the excess is dropped, as on Windows
    const int room = impl->rate / 4 - queued_frames();
    frames = std::min(frames, room);
    if (frames <= 0) return;
    if (impl->volume < 0.999f) {
        impl->tmp.assign(stereo, stereo + frames * 2);
        for (int16_t& s : impl->tmp) s = (int16_t)(s * impl->volume);
        stereo = impl->tmp.data();
    }
    SDL_QueueAudio(impl->dev, stereo, (Uint32)frames * 4);
}

int Audio::queued_frames() const { return impl->dev ? (int)(SDL_GetQueuedAudioSize(impl->dev) / 4) : 0; }
int Audio::sample_rate() const { return impl->rate; }
void Audio::set_paused(bool paused) { if (impl->dev) SDL_PauseAudioDevice(impl->dev, paused ? 1 : 0); }
void Audio::set_master_volume(float v) { impl->volume = std::max(0.0f, std::min(v, 1.0f)); }
