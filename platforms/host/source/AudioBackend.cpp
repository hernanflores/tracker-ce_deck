#include "AudioBackend.h"

#include "HostPlatform.h"
#include "Tracker.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

static constexpr int kBlockFrames = 128;
static constexpr int kSampleRate = 44100;
static constexpr int kBytesPerFrame = 4; // s16 stereo
static constexpr int kBlockBytes = kBlockFrames * kBytesPerFrame;

namespace
{
struct IsrContext
{
    Tracker* tracker;
    uint32_t inI2s[kBlockFrames];
    uint32_t inUsb[kBlockFrames];
    int16_t left[kBlockFrames];
    int16_t right[kBlockFrames];
};

void isrBody(void* arg)
{
    IsrContext* c = (IsrContext*)arg;
    c->tracker->updateInputAudioBuffers(c->inI2s, c->inUsb, kBlockFrames);
    c->tracker->updateAudioBuffers(c->left, c->right, kBlockFrames);
}

void SDLCALL outputCallback(void* userdata, SDL_AudioStream* stream, int additional, int)
{
    ((AudioBackend*)userdata)->onOutputRequest(stream, additional);
}
} // namespace

bool AudioBackend::start(Tracker* t, Sgtl5000Host* c, const Options& o)
{
    tracker = t;
    codec = c;
    options = o;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }

    // ask the device for small buffers; the cushion we keep queued is separate (targetFrames)
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");

    SDL_AudioSpec spec = {};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = kSampleRate;

    if (options.enableInput)
    {
        in = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
        if (in == nullptr)
            fprintf(stderr, "audio input unavailable: %s\n", SDL_GetError());
        else
            SDL_ResumeAudioStreamDevice(in);
    }

    out = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, outputCallback, this);
    if (out == nullptr)
    {
        fprintf(stderr, "audio output unavailable: %s\n", SDL_GetError());
        stop();
        return false;
    }
    SDL_ResumeAudioStreamDevice(out);
    return true;
}

void AudioBackend::stop()
{
    // destroying a stream waits for a running callback to finish
    if (out != nullptr)
    {
        SDL_DestroyAudioStream(out);
        out = nullptr;
    }
    if (in != nullptr)
    {
        SDL_DestroyAudioStream(in);
        in = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

const char* AudioBackend::driverName() const
{
    const char* n = SDL_GetCurrentAudioDriver();
    return n ? n : "none";
}

AudioBackend::Stats AudioBackend::stats() const
{
    Stats s;
    s.blocks = blocks;
    s.underruns = underruns;
    s.lateBlocks = lateBlocks;
    s.maxRenderUs = maxRenderUs;
    s.maxGateWaitUs = IrqGate::maxWaitUs();
    s.inputUnderflows = inputUnderflows;
    return s;
}

void AudioBackend::renderBlock(int16_t* interleavedOut)
{
    static thread_local IsrContext ctx;
    ctx.tracker = tracker;

    // input: one block of interleaved s16 stereo, silence when the device has nothing yet
    memset(ctx.inI2s, 0, sizeof(ctx.inI2s));
    memset(ctx.inUsb, 0, sizeof(ctx.inUsb));
    if (in != nullptr)
    {
        const int got = SDL_GetAudioStreamData(in, ctx.inI2s, kBlockBytes);
        if (got < kBlockBytes) inputUnderflows++;
        // keep the recording queue from growing without bound if we fall behind
        while (SDL_GetAudioStreamAvailable(in) > kBlockBytes * 16)
        {
            uint32_t discard[kBlockFrames];
            if (SDL_GetAudioStreamData(in, discard, kBlockBytes) <= 0) break;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    IrqGate::runIsr(isrBody, &ctx);
    const auto renderUs = (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();

    uint32_t prev = maxRenderUs;
    while (renderUs > prev && !maxRenderUs.compare_exchange_weak(prev, renderUs)) {}
    if (renderUs > 1000000ull * kBlockFrames / kSampleRate) lateBlocks++;
    blocks++;

    // software master volume / mute, ramped across the block to avoid clicks
    const bool muted = codec->headphoneMuted.load() || gAudioOutMuted.load();
    const float target = muted ? 0.0f : std::clamp(codec->masterVolume.load(), 0.0f, 1.0f);
    const float step = (target - lastGain) / kBlockFrames;
    float g = lastGain;
    for (int i = 0; i < kBlockFrames; i++)
    {
        g += step;
        interleavedOut[i * 2] = (int16_t)(ctx.left[i] * g);
        interleavedOut[i * 2 + 1] = (int16_t)(ctx.right[i] * g);
    }
    lastGain = target;
}

void AudioBackend::onOutputRequest(SDL_AudioStream* stream, int additionalBytes)
{
    const int targetBytes = options.targetFrames * kBytesPerFrame;
    if (SDL_GetAudioStreamQueued(stream) == 0 && additionalBytes > 0 && blocks > 0) underruns++;

    int16_t buf[kBlockFrames * 2];
    int need = additionalBytes;
    while (need > 0 || SDL_GetAudioStreamQueued(stream) < targetBytes)
    {
        renderBlock(buf);
        SDL_PutAudioStreamData(stream, buf, kBlockBytes);
        need -= kBlockBytes;
    }
}
