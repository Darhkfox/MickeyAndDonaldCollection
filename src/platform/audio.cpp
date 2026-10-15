// XAudio2 2.9 streaming output (see audio.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "audio.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace {
const int kBuffers = 32;        // ring of submit buffers (XAudio2 allows up to 64 queued)
const int kBufFrames = 2048;    // max frames per submitted buffer
}

struct Audio::Impl : public IXAudio2EngineCallback {
    IXAudio2* xa = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    int rate = 48000;
    bool paused = false, com_init = false, active = false;
    float volume = 1.0f;
    volatile LONG critical = 0;             // set by XAudio2 on device loss
    ULONGLONG next_retry = 0;
    std::vector<int16_t> ring;              // kBuffers * kBufFrames stereo frames
    int buf_frames[kBuffers] = {};
    int next = 0;
    uint64_t submitted = 0;                 // frames submitted since the voice was created

    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { InterlockedExchange(&critical, 1); }

    bool open() {
        if (FAILED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        xa->RegisterForCallbacks(this);
        if (FAILED(xa->CreateMasteringVoice(&master))) return false;
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = (DWORD)rate;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = (DWORD)rate * 4;
        if (FAILED(xa->CreateSourceVoice(&voice, &wf))) return false;
        master->SetVolume(volume);
        submitted = 0; next = 0;
        if (!paused) voice->Start(0);
        return true;
    }
    void close() {
        if (voice) { voice->DestroyVoice(); voice = nullptr; }
        if (master) { master->DestroyVoice(); master = nullptr; }
        if (xa) { xa->UnregisterForCallbacks(this); xa->StopEngine(); xa->Release(); xa = nullptr; }
        critical = 0;
    }
    // Best-effort recovery after the audio device disappeared (retried at most once per second).
    bool ready() {
        if (!active) return false;
        if (critical || !voice) {
            ULONGLONG now = GetTickCount64();
            if (now < next_retry) return false;
            close();
            if (!open()) { close(); next_retry = now + 1000; return false; }
        }
        return true;
    }
    int queued(const XAUDIO2_VOICE_STATE& st) const {
        // Upper bound: frames in the buffers still queued (guards against SamplesPlayed resets).
        int upper = 0;
        for (UINT32 i = 0; i < st.BuffersQueued && i < (UINT32)kBuffers; i++)
            upper += buf_frames[(next - 1 - (int)i + kBuffers * 2) % kBuffers];
        long long q = (long long)(submitted - st.SamplesPlayed);
        return (int)std::max(0LL, std::min(q, (long long)upper));
    }
};

Audio::Audio() : impl(new Impl) {}
Audio::~Audio() { shutdown(); delete impl; }

bool Audio::init(int sample_rate) {
    shutdown();
    Impl& I = *impl;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    I.com_init = SUCCEEDED(hr);    // RPC_E_CHANGED_MODE: COM already up in another mode, fine
    I.rate = sample_rate > 0 ? sample_rate : 48000;
    I.ring.assign((size_t)kBuffers * kBufFrames * 2, 0);
    I.active = true;
    if (!I.open()) { I.close(); I.active = false; return false; }
    return true;
}

void Audio::shutdown() {
    Impl& I = *impl;
    I.close();
    I.active = false;
    I.ring.clear();
    if (I.com_init) { CoUninitialize(); I.com_init = false; }
}

void Audio::push(const int16_t* stereo, int frames) {
    Impl& I = *impl;
    if (!stereo || frames <= 0 || !I.ready()) return;
    XAUDIO2_VOICE_STATE st;
    I.voice->GetState(&st, 0);
    int room = I.rate / 4 - I.queued(st);    // cap the queue at ~250 ms, drop the rest
    frames = std::min(frames, room);
    int busy = (int)st.BuffersQueued;
    while (frames > 0 && busy < kBuffers) {
        int n = std::min(frames, kBufFrames);
        int16_t* dst = &I.ring[(size_t)I.next * kBufFrames * 2];
        memcpy(dst, stereo, (size_t)n * 4);
        XAUDIO2_BUFFER b = {};
        b.AudioBytes = (UINT32)n * 4;
        b.pAudioData = (const BYTE*)dst;
        if (FAILED(I.voice->SubmitSourceBuffer(&b))) break;
        I.buf_frames[I.next] = n;
        I.next = (I.next + 1) % kBuffers;
        I.submitted += (uint64_t)n;
        busy++;
        stereo += n * 2;
        frames -= n;
    }
}

int Audio::queued_frames() const {
    Impl& I = *impl;
    if (!I.voice || I.critical) return 0;
    XAUDIO2_VOICE_STATE st;
    I.voice->GetState(&st, 0);
    return I.queued(st);
}

int Audio::sample_rate() const { return impl->rate; }

void Audio::set_paused(bool paused) {
    Impl& I = *impl;
    if (I.paused == paused) return;
    I.paused = paused;
    if (!I.voice) return;
    if (paused) I.voice->Stop(0); else I.voice->Start(0);
}

void Audio::set_master_volume(float v) {
    Impl& I = *impl;
    I.volume = std::max(0.0f, std::min(v, 1.0f));
    if (I.master) I.master->SetVolume(I.volume);
}
