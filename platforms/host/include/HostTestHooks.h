#ifndef HOST_TEST_HOOKS_H_
#define HOST_TEST_HOOKS_H_

#include <string>

// Scriptable actions for headless runs (smoke tests, audio parity exports). They use the same
// file manager entry points the UI uses, driven from the main loop.
struct HostTestHooks
{
    // project to open before doing anything else, e.g. path "Projects", name "MySong"
    std::string openPath;
    std::string openName;
    // "", "song" or "pattern": export once the project is ready
    std::string exportMode;
    // generate a sine sample, import it into instrument 1 and put notes in pattern 1 (so an
    // export is not silent); runs after the project is open
    bool demoSong = false;
    // start playing pattern 1 once ready (use with --run-seconds)
    bool play = false;

    enum class Result
    {
        kRunning,
        kDone,
        kFailed
    };

    // call every main-loop iteration; returns kDone/kFailed when the script has finished
    Result tick(unsigned nowMs);

    bool active() const { return !openName.empty() || !exportMode.empty() || demoSong || play; }

  private:
    enum State
    {
        kWaitReady,
        kOpening,
        kImportingDemo,
        kExporting,
        kFinished
    };
    State state = kWaitReady;
    unsigned stateSinceMs = 0;
    bool started = false;

    Result startExport(unsigned nowMs);
    bool startDemoImport();
    void placeDemoNotes();
};

#endif
