#include "HostPlatform.h"

#include <thread>

#ifndef BUILD_ID
#define BUILD_ID "host"
#endif

Platform::SystemInfo PlatformHost::systemInfo = { BUILD_ID };

//----------------------------------------------------------------------------------------------
TimerHost::TimerHost() : start(std::chrono::steady_clock::now())
{
}

uint32_t TimerHost::millis()
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

uint32_t TimerHost::micros()
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<microseconds>(steady_clock::now() - start).count();
}

void TimerHost::delay(int ms)
{
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

uint32_t TimerHost::clockCycles()
{
    // 1 "cycle" == 1 ns
    using namespace std::chrono;
    return (uint32_t)duration_cast<nanoseconds>(steady_clock::now() - start).count();
}

uint32_t TimerHost::clockCyclesPerUs()
{
    return 1000;
}

//----------------------------------------------------------------------------------------------
// progressing hold intervals, copied from platforms/imx/source/Drivers/Keys.cpp
static constexpr uint16_t kHoldTimeStepDiff[] = { 0, 100, 190, 235, 255, 270 };
static constexpr int kHoldStepsCount = sizeof(kHoldTimeStepDiff) / sizeof(kHoldTimeStepDiff[0]);

ButtonsHost::ButtonsHost(Timer* timer) : timer(timer)
{
    for (auto& p : pressedAtomic) p = false;
}

void ButtonsHost::setHoldTime(int timeMs)
{
    holdTime = (uint16_t)timeMs;
}

bool ButtonsHost::isButtonPressed(int buttonIdx)
{
    if (buttonIdx < 0 || buttonIdx >= kMaxButtons) return false;
    return pressedAtomic[buttonIdx].load();
}

void ButtonsHost::setActionCallback(ActionType action, void (*callback)(uint8_t))
{
    callbacks[action] = callback;
}

void ButtonsHost::postEvent(int buttonIdx, bool pressed)
{
    if (buttonIdx < 0 || buttonIdx >= kMaxButtons) return;
    std::lock_guard<std::mutex> lock(queueMutex);
    queue.push_back({ (uint8_t)buttonIdx, pressed });
}

void ButtonsHost::update()
{
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        events.swap(queue);
    }

    const uint32_t now = timer->millis();
    for (const Event& e : events)
    {
        if (e.pressed == pressedState[e.idx]) continue;
        pressedState[e.idx] = e.pressed;
        pressedAtomic[e.idx] = e.pressed;
        if (e.pressed)
        {
            holdStartMs[e.idx] = now;
            holdFactor[e.idx] = 0;
        }
        if (auto cb = callbacks[e.pressed ? kPress : kRelease]) cb(e.idx);
    }

    for (int i = 0; i < kMaxButtons; i++)
    {
        if (!pressedState[i]) continue;
        if (now - holdStartMs[i] > (uint32_t)(holdTime - kHoldTimeStepDiff[holdFactor[i]]))
        {
            if (holdFactor[i] < kHoldStepsCount - 1) holdFactor[i]++;
            holdStartMs[i] = now;
            if (auto cb = callbacks[kHold]) cb((uint8_t)i);
        }
    }
}

//----------------------------------------------------------------------------------------------
PlatformHost::PlatformHost(const std::string& sdRoot, bool)
    : fileSystem(sdRoot), buttons(&timer), displayDriver(&nullDriver)
{
}

void PlatformHost::init()
{
    uartMidi.init();
    fileSystem.init();
    configManager.init(&fileSystem);
}

void PlatformHost::update()
{
    fileSystem.update();
}

void PlatformHost::setPowerMode(bool mode)
{
    if (!mode) quitRequested = true;
}

void PlatformHost::resetMCU()
{
    restartRequested = true;
    quitRequested = true;
}

void PlatformHost::runBootloader(char*)
{
    // firmware updates do not apply to the desktop build
}

bool PlatformHost::readForceStartUp()
{
    fileSystem.init();
    if (fileSystem.exists(kTurnOnFilePath))
    {
        fileSystem.remove(kTurnOnFilePath);
        return true;
    }
    return false;
}
