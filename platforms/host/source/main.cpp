// Desktop entry point: runs the Tracker Mini firmware core on top of the host platform.
#include "HostPlatform.h"
#include "Tracker.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

static constexpr int kAudioBlockFrames = 128;
static constexpr int kSampleRate = 44100;

struct Options
{
    std::string sdRoot;
    std::string display = "null";
    double runSeconds = 0; // 0 = run until the firmware powers off
};

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--sd <dir>] [--display=null] [--run-seconds N]\n"
            "  --sd <dir>        directory used as the SD card root (default ~/TrackerMini)\n"
            "  --display=null    no display output (only mode available so far)\n"
            "  --run-seconds N   quit after N seconds (smoke tests)\n",
            argv0);
}

static bool parseArgs(int argc, char** argv, Options& o)
{
    const char* home = getenv("HOME");
    o.sdRoot = std::string(home ? home : ".") + "/TrackerMini";
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--sd" && i + 1 < argc)
            o.sdRoot = argv[++i];
        else if (a.rfind("--display=", 0) == 0)
            o.display = a.substr(10);
        else if (a == "--run-seconds" && i + 1 < argc)
            o.runSeconds = atof(argv[++i]);
        else
            return false;
    }
    return o.display == "null";
}

//----------------------------------------------------------------------------------------------
// Headless audio pump: renders 128-frame blocks in real time inside the emulated audio interrupt.
//----------------------------------------------------------------------------------------------
static std::atomic<bool> gStopAudio{ false };

static void audioIsr(void* arg)
{
    Tracker* tracker = (Tracker*)arg;
    static uint32_t inI2s[kAudioBlockFrames] = {};
    static uint32_t inUsb[kAudioBlockFrames] = {};
    static int16_t left[kAudioBlockFrames];
    static int16_t right[kAudioBlockFrames];
    tracker->updateInputAudioBuffers(inI2s, inUsb, kAudioBlockFrames);
    tracker->updateAudioBuffers(left, right, kAudioBlockFrames);
}

static void audioPump(Tracker* tracker)
{
    using clock = std::chrono::steady_clock;
    const auto block = std::chrono::nanoseconds(1000000000LL * kAudioBlockFrames / kSampleRate);
    auto next = clock::now();
    while (!gStopAudio)
    {
        IrqGate::runIsr(audioIsr, tracker);
        next += block;
        std::this_thread::sleep_until(next);
    }
}

int main(int argc, char** argv)
{
    Options opt;
    if (!parseArgs(argc, argv, opt))
    {
        usage(argv[0]);
        return 2;
    }

    PlatformHost platform(opt.sdRoot, true);
    platform.init();

    Tracker* tracker = Tracker::getInstance();
    tracker->getStartDisplayFunc()(&platform);
    tracker->init(&platform);

    std::thread audio(audioPump, tracker);

    const auto t0 = std::chrono::steady_clock::now();
    while (!platform.quitRequested)
    {
        platform.update();
        platform.hostButtons().update();
        tracker->update();

        if (opt.runSeconds > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >
              opt.runSeconds)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    gStopAudio = true;
    audio.join();

    if (platform.restartRequested)
    {
        execv(argv[0], argv);
    }
    return 0;
}
