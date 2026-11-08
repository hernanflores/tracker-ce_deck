#include "HostTestHooks.h"

#include "fileManager.h"
#include "mtAudioExport.h"
#include "Tracker.h"
#include "mtSequencer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static bool fileManagerIdle()
{
    return newFileManager.getStatus() == fmIdle &&
           newFileManager.getCurrentOperation() == fmNoOperation;
}

// 16-bit mono PCM WAV with a 220 Hz sine, one second, written through the card file system
static bool writeSineWav(FileSystem* fs, const char* path)
{
    constexpr uint32_t kRate = 44100;
    constexpr uint32_t kFrames = kRate;
    std::vector<uint8_t> wav(44 + kFrames * 2);
    auto put32 = [&](size_t o, uint32_t v) { memcpy(&wav[o], &v, 4); };
    auto put16 = [&](size_t o, uint16_t v) { memcpy(&wav[o], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, 36 + kFrames * 2);
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, kRate);
    put32(28, kRate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, kFrames * 2);
    for (uint32_t i = 0; i < kFrames; i++)
    {
        int16_t v = (int16_t)lround(12000.0 * sin(2.0 * M_PI * 220.0 * i / kRate));
        memcpy(&wav[44 + i * 2], &v, 2);
    }

    fs->mkdir(0, "HostTest");
    auto [ok, file] = fs->openFile(path, FileSystem::kWriteCreateNew);
    if (!ok) return false;
    fs->writeFile(wav.data(), wav.size(), file);
    fs->closeFile(file);
    return true;
}

bool HostTestHooks::startDemoImport()
{
    FileSystem* fs = Tracker::getInstance()->getPlatform()->getFileSystem();
    if (!writeSineWav(fs, "HostTest/sine.wav")) return false;

    char path[] = "HostTest";
    char name[] = "sine";
    return newFileManager.importSampleToProject(path, name, 0);
}

void HostTestHooks::placeDemoNotes()
{
    Sequencer::strPattern* pattern = sequencer.getPatternToUI();
    for (int step = 0; step < 16; step += 4)
    {
        auto& s = pattern->track[0].step[step];
        s.note = 60;
        s.instrument = 0;
    }
}

HostTestHooks::Result HostTestHooks::startExport(unsigned nowMs)
{
    if (exportMode.empty()) return Result::kDone;

    const uint8_t mode = exportMode == "pattern" ? exportPattern : exportSong;
    if (!newFileManager.exportSound(mode))
    {
        fprintf(stderr, "[hook] exportSound refused\n");
        return Result::kFailed;
    }
    fprintf(stderr, "[hook] export %s started\n", exportMode.c_str());
    state = kExporting;
    stateSinceMs = nowMs;
    return Result::kRunning;
}

HostTestHooks::Result HostTestHooks::tick(unsigned nowMs)
{
    constexpr unsigned kStartupSettleMs = 1500;
    constexpr unsigned kStepTimeoutMs = 120000;

    if (!started)
    {
        started = true;
        stateSinceMs = nowMs;
    }
    const bool timedOut = nowMs - stateSinceMs > kStepTimeoutMs;
    auto wait = [&]() { return timedOut ? Result::kFailed : Result::kRunning; };

    // after the optional project open, continue with the demo import / export
    auto afterOpen = [&]() -> Result {
        if (demoSong)
        {
            if (!startDemoImport())
            {
                fprintf(stderr, "[hook] demo sample import refused\n");
                return Result::kFailed;
            }
            fprintf(stderr, "[hook] importing demo sample\n");
            state = kImportingDemo;
            stateSinceMs = nowMs;
            return Result::kRunning;
        }
        if (play)
        {
            sequencer.playPattern();
            fprintf(stderr, "[hook] playing pattern\n");
            state = kFinished;
            return Result::kRunning;
        }
        return startExport(nowMs);
    };

    switch (state)
    {
        case kWaitReady:
            if (nowMs < kStartupSettleMs || !fileManagerIdle()) return wait();

            if (!openName.empty())
            {
                char path[256], name[64];
                snprintf(path, sizeof(path), "%s", openPath.c_str());
                snprintf(name, sizeof(name), "%s", openName.c_str());
                if (!newFileManager.openProject(path, name))
                {
                    fprintf(stderr, "[hook] openProject(%s, %s) refused\n", path, name);
                    return Result::kFailed;
                }
                fprintf(stderr, "[hook] opening project %s/%s\n", path, name);
                state = kOpening;
                stateSinceMs = nowMs;
                return Result::kRunning;
            }
            return afterOpen();

        case kOpening:
            if (!fileManagerIdle()) return wait();
            fprintf(stderr, "[hook] project open\n");
            return afterOpen();

        case kImportingDemo:
            if (!fileManagerIdle()) return wait();
            fprintf(stderr, "[hook] instrument 1: active %d type %d length %u name '%s'\n", mtProject.instrument[0].isActive, mtProject.instrument[0].sample.type, (unsigned)mtProject.instrument[0].sample.length, mtProject.instrument[0].sample.file_name);
            {
                auto& sm = mtProject.sampleMemory[0];
                int16_t* a = sm.address[0];
                fprintf(stderr, "[hook] sampleMemory[0]: addr %p streamed %d stereo %d ramFrames %u pre %u first=%d,%d mid=%d\n", (void*)a, sm.isStreamed, sm.isStereo, (unsigned)sm.ramFrames, (unsigned)sm.preCacheFrames, a ? a[0] : 0, a ? a[1] : 0, a ? a[5000] : 0);
            }
            placeDemoNotes();
            fprintf(stderr, "[hook] demo notes placed\n");
            if (play)
            {
                sequencer.playPattern();
                fprintf(stderr, "[hook] playing pattern\n");
                state = kFinished;
                return Result::kRunning;
            }
            return startExport(nowMs);

        case kExporting:
            // the editors normally acknowledge the end state; do it here when no editor does
            if (newFileManager.getStatus() == fmExportSoundEnd) newFileManager.clearStatus();
            if (!fileManagerIdle())
            {
                static unsigned lastReport = 0;
                if (nowMs - lastReport > 1000)
                {
                    lastReport = nowMs;
                    fprintf(stderr, "[hook] export progress %d%% (fm status %d op %d, exporter %d)\n", exporter.getProgress(), newFileManager.getStatus(), newFileManager.getCurrentOperation(), exporter.getStatus());
                }
                return wait();
            }
            fprintf(stderr, "[hook] export finished\n");
            state = kFinished;
            return Result::kDone;

        case kFinished:
            return play ? Result::kRunning : Result::kDone;
    }
    return Result::kFailed;
}
