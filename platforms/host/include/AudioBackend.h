#ifndef HOST_AUDIO_BACKEND_H_
#define HOST_AUDIO_BACKEND_H_

#include <atomic>
#include <cstdint>

class Tracker;
class Sgtl5000Host;

struct SDL_AudioStream;

// SDL3 audio output (and optional input). The audio callback renders exact 128-frame blocks of the
// firmware audio engine, each one inside the emulated audio interrupt (IrqGate::runIsr).
class AudioBackend
{
  public:
    struct Options
    {
        int targetFrames = 512; // output cushion kept queued ahead of the device (256-512 typical)
        bool enableInput = false;
    };

    struct Stats
    {
        uint64_t blocks = 0;        // 128-frame blocks rendered
        uint64_t underruns = 0;     // device wanted data and nothing was queued
        uint64_t lateBlocks = 0;    // a block took longer to render than its own duration
        uint32_t maxRenderUs = 0;   // slowest block
        uint32_t maxGateWaitUs = 0; // longest wait for the interrupt gate (main thread had IRQs off)
        uint64_t inputUnderflows = 0;
    };

    ~AudioBackend() { stop(); }

    bool start(Tracker* tracker, Sgtl5000Host* codec, const Options& options);
    void stop();
    Stats stats() const;
    const char* driverName() const;

    // used by the SDL callbacks
    void onOutputRequest(SDL_AudioStream* stream, int additionalBytes);

  private:
    void renderBlock(int16_t* interleavedOut);

    Tracker* tracker = nullptr;
    Sgtl5000Host* codec = nullptr;
    Options options;
    SDL_AudioStream* out = nullptr;
    SDL_AudioStream* in = nullptr;

    float lastGain = 1.0f;
    std::atomic<uint64_t> blocks{ 0 }, underruns{ 0 }, lateBlocks{ 0 }, inputUnderflows{ 0 };
    std::atomic<uint32_t> maxRenderUs{ 0 };
};

#endif
