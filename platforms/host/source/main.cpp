// Desktop entry point: runs the Tracker Mini firmware core on top of the host platform.
#include "AudioBackend.h"
#include "HostPlatform.h"
#include "HostTestHooks.h"
#include "Presenter.h"
#include "EveHost.h"
#include "eve/BtEmuDevice.h"
#include "eve/PassiveEveDevice.h"
#include "eve/TraceTee.h"
#include "Tracker.h"

#include <algorithm>
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
    std::string display = "null"; // null | passive | pattern | bteve
    std::string eveTrace;         // record the display bus stream to this file
    std::string eveLib = "libbt8xxemu.so";
    double scale = 1.6;
    bool fullscreen = false;
    double runSeconds = 0; // 0 = run until the firmware powers off
    std::string audio = "sdl"; // sdl | pump (headless real-time pump, no device)
    int audioBuffer = 512;     // frames kept queued ahead of the device
    bool audioIn = false;
    bool audioStats = false;
    HostTestHooks hooks;
};

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--sd <dir>] [--display=MODE] [--eve-trace FILE] [--run-seconds N]\n"
            "          [--eve-lib PATH] [--scale X] [--fullscreen]\n"
            "          [--audio=sdl|pump] [--audio-buffer FRAMES] [--audio-in] [--audio-stats]\n"
            "          [--open-project <path> <name>] [--export song|pattern]\n"
            "  --sd <dir>        directory used as the SD card root (default ~/TrackerMini)\n"
            "  --display=MODE    null (no display, default), passive (firmware drives a chip that\n"
            "                    ignores commands), pattern (passive + test pattern window),\n"
            "                    bteve (Bridgetek emulator, Linux x86_64, user-installed)\n"
            "  --eve-trace FILE  record the display bus stream (for eve-replay)\n"
            "  --eve-lib PATH    libbt8xxemu.so for --display=bteve\n"
            "  --scale X         window size relative to 800x480 (default 1.6)\n"
            "  --fullscreen      start fullscreen\n"
            "  --run-seconds N   quit after N seconds (smoke tests)\n"
            "  --audio=MODE      sdl (default) or pump (real-time rendering without a device)\n"
            "  --audio-buffer N  output cushion in frames (default 512)\n"
            "  --audio-in        open the default recording device (line/mic input)\n"
            "  --audio-stats     print audio underrun/latency counters every 10 s and at exit\n"
            "  --open-project    open <path>/<name> on the card (e.g. Projects MySong)\n"
            "  --demo-song       import a generated sine sample and add notes to pattern 1\n"
            "  --export M        export the open project (song or pattern) into Export/,\n"
            "                    then quit (exit status 0 on success)\n",
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
        else if (a == "--eve-trace" && i + 1 < argc)
            o.eveTrace = argv[++i];
        else if (a == "--eve-lib" && i + 1 < argc)
            o.eveLib = argv[++i];
        else if (a == "--scale" && i + 1 < argc)
            o.scale = atof(argv[++i]);
        else if (a == "--fullscreen")
            o.fullscreen = true;
        else if (a == "--run-seconds" && i + 1 < argc)
            o.runSeconds = atof(argv[++i]);
        else if (a == "--open-project" && i + 2 < argc)
        {
            o.hooks.openPath = argv[++i];
            o.hooks.openName = argv[++i];
        }
        else if (a.rfind("--audio=", 0) == 0)
            o.audio = a.substr(8);
        else if (a == "--audio-buffer" && i + 1 < argc)
            o.audioBuffer = std::max(128, atoi(argv[++i]));
        else if (a == "--audio-in")
            o.audioIn = true;
        else if (a == "--audio-stats")
            o.audioStats = true;
        else if (a == "--play")
            o.hooks.play = true;
        else if (a == "--demo-song")
            o.hooks.demoSong = true;
        else if (a == "--export" && i + 1 < argc)
            o.hooks.exportMode = argv[++i];
        else
            return false;
    }
    if (!o.hooks.exportMode.empty() && o.hooks.exportMode != "song" &&
        o.hooks.exportMode != "pattern")
        return false;
    return (o.display == "null" || o.display == "passive" || o.display == "pattern" || o.display == "bteve") && (o.audio == "sdl" || o.audio == "pump");
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

    static int peak = 0;
    static int blocks = 0;
    for (int i = 0; i < kAudioBlockFrames; i++)
    {
        peak = std::max(peak, std::max(abs(left[i]), abs(right[i])));
    }
    if (getenv("HOST_AUDIO_PEAK") && ++blocks == kSampleRate / kAudioBlockFrames)
    {
        fprintf(stderr, "[audio] peak last second: %d\n", peak);
        peak = 0;
        blocks = 0;
    }
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

    // display: the firmware's FT812 library always talks to an EveDevice (except --display=null)
    DisplayDriverHost hostDisplay;
    Presenter presenter;
    std::unique_ptr<EveDevice> eve;
    if (opt.display != "null")
    {
        if (opt.display == "bteve")
        {
            std::string err;
            eve = makeBtEmuDevice(opt.eveLib, err);
            if (!eve)
            {
                fprintf(stderr, "display: %s\n", err.c_str());
                return 3;
            }
        }
        else
        {
            eve = std::make_unique<PassiveEveDevice>(opt.display == "pattern");
        }
        if (!opt.eveTrace.empty())
        {
            auto tee = std::make_unique<TraceTee>(std::move(eve), opt.eveTrace);
            if (!tee->ok())
            {
                fprintf(stderr, "cannot write trace %s\n", opt.eveTrace.c_str());
                return 3;
            }
            eve = std::move(tee);
        }
        if (!hostDisplay.init(eve.get()))
        {
            fprintf(stderr, "display: chip did not answer\n");
            return 3;
        }
        platform.setDisplayDriver(&hostDisplay);
        fprintf(stderr, "display: %s\n", eve->name());
        if (opt.display == "pattern" || opt.display == "bteve")
        {
            if (!presenter.init(opt.scale, opt.fullscreen)) return 3;
        }
    }

    Tracker* tracker = Tracker::getInstance();
    tracker->getStartDisplayFunc()(&platform);
    tracker->init(&platform);

    AudioBackend audioBackend;
    std::thread pumpThread;
    bool useSdl = opt.audio == "sdl";
    if (useSdl)
    {
        AudioBackend::Options ao;
        ao.targetFrames = opt.audioBuffer;
        ao.enableInput = opt.audioIn;
        if (audioBackend.start(tracker, &platform.hostCodec(), ao))
        {
            fprintf(stderr, "audio: SDL driver '%s', cushion %d frames\n", audioBackend.driverName(), opt.audioBuffer);
        }
        else
        {
            fprintf(stderr, "audio: falling back to the headless pump\n");
            useSdl = false;
        }
    }
    if (!useSdl) pumpThread = std::thread(audioPump, tracker);

    auto printStats = [&](const char* tag) {
        if (!useSdl) return;
        auto s = audioBackend.stats();
        fprintf(stderr,
                "[audio %s] blocks %llu underruns %llu late %llu maxRender %uus maxGateWait %uus inUnderflow %llu\n",
                tag, (unsigned long long)s.blocks, (unsigned long long)s.underruns,
                (unsigned long long)s.lateBlocks, s.maxRenderUs, s.maxGateWaitUs,
                (unsigned long long)s.inputUnderflows);
    };
    unsigned lastStatsMs = 0;

    int exitCode = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (!platform.quitRequested)
    {
        platform.update();
        platform.hostButtons().update();
        tracker->update();

        if (presenter.active())
        {
            EveFrame frame;
            if (eve->pollFrame(frame)) presenter.present(frame);
            if (!presenter.pumpEvents()) platform.quitRequested = true;
        }

        if (opt.audioStats && platform.getTimer()->millis() - lastStatsMs > 10000)
        {
            lastStatsMs = platform.getTimer()->millis();
            printStats("stats");
        }

        if (opt.hooks.active())
        {
            auto r = opt.hooks.tick(platform.getTimer()->millis());
            if (r != HostTestHooks::Result::kRunning)
            {
                exitCode = r == HostTestHooks::Result::kDone ? 0 : 1;
                break;
            }
        }

        if (opt.runSeconds > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >
              opt.runSeconds)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    gStopAudio = true;
    if (pumpThread.joinable()) pumpThread.join();
    if (opt.audioStats) printStats("final");
    audioBackend.stop();

    if (platform.restartRequested)
    {
        execv(argv[0], argv);
    }
    return exitCode;
}
