// Emulates the interrupt semantics of the MCU on a multi-threaded desktop process.
//
// - The audio thread plays the role of the audio DMA interrupt. It holds the gate for a whole
//   block, and while it does, every disable/enable call made by the firmware is a no-op (an
//   interrupt cannot be preempted by the main loop).
// - The main thread gets CPSID/CPSIE (and NVIC_Disable/EnableIRQ) behaviour: `disable` takes the
//   gate unless it already holds it, `enable` releases it unconditionally. There is deliberately
//   no nesting counter, exactly like on the hardware.
#include "HostPlatform.h"

#include "interruptsControl.h"

static std::mutex gGate;
static thread_local bool tInIsr = false;
static std::atomic<bool> gMainHolds{ false };
static std::atomic<uint32_t> gMaxWaitUs{ 0 };

namespace IrqGate
{
bool inIsr()
{
    return tInIsr;
}

uint32_t maxWaitUs()
{
    return gMaxWaitUs.load();
}

void resetStats()
{
    gMaxWaitUs = 0;
}

void runIsr(void (*isr)(void*), void* arg)
{
    const auto t0 = std::chrono::steady_clock::now();
    gGate.lock();
    const auto waitUs = (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    uint32_t prev = gMaxWaitUs.load();
    while (waitUs > prev && !gMaxWaitUs.compare_exchange_weak(prev, waitUs)) {}
    tInIsr = true;
    isr(arg);
    tInIsr = false;
    gGate.unlock();
}
} // namespace IrqGate

static void gateDisable()
{
    if (tInIsr) return;
    if (!gMainHolds.load(std::memory_order_relaxed))
    {
        gGate.lock();
        gMainHolds.store(true, std::memory_order_relaxed);
    }
}

static void gateEnable()
{
    if (tInIsr) return;
    if (gMainHolds.load(std::memory_order_relaxed))
    {
        gMainHolds.store(false, std::memory_order_relaxed);
        gGate.unlock();
    }
}

extern "C" void hostIrqDisable(void)
{
    gateDisable();
}

extern "C" void hostIrqEnable(void)
{
    gateEnable();
}

void enableAudioInterrupt()
{
    gateEnable();
}

void disableAudioInterrupt()
{
    gateDisable();
}
