// IrqGate semantics: no nesting counter on the main thread, interrupts cannot be preempted.
#include "HostPlatform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

extern "C" void hostIrqDisable(void);
extern "C" void hostIrqEnable(void);

static int failures = 0;
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

using namespace std::chrono_literals;

static std::atomic<int> isrRuns{ 0 };
static void isr(void*)
{
    isrRuns++;
}

// runs one "interrupt" on another thread; true if it completed within `wait`
static bool isrCompletesWithin(std::chrono::milliseconds wait)
{
    const int before = isrRuns;
    std::thread t([] { IrqGate::runIsr(isr, nullptr); });
    const auto end = std::chrono::steady_clock::now() + wait;
    while (isrRuns == before && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    const bool done = isrRuns > before;
    // if it did not complete it is blocked on the gate: the caller must release it, then we join
    if (!done) hostIrqEnable();
    t.join();
    if (!done) hostIrqDisable();
    return done;
}

// a result on a freshly started thread (thread_local ISR flag is per thread)
int main()
{
    // interrupts enabled: the ISR runs
    CHECK(isrCompletesWithin(500ms));

    // disabled: the ISR is held off
    hostIrqDisable();
    CHECK(!isrCompletesWithin(100ms));
    hostIrqEnable();

    // no counting: disable, disable, enable leaves interrupts enabled (like CPSID/CPSIE)
    hostIrqDisable();
    hostIrqDisable();
    hostIrqEnable();
    CHECK(isrCompletesWithin(500ms));

    // enable without a prior disable is harmless
    hostIrqEnable();
    hostIrqEnable();
    CHECK(isrCompletesWithin(500ms));

    // inside the ISR, disable/enable are no-ops and do not release or deadlock the gate
    std::atomic<bool> nestedOk{ false };
    std::thread t([&] {
        IrqGate::runIsr(
          [](void* p) {
              CHECK(IrqGate::inIsr());
              hostIrqDisable();
              hostIrqDisable();
              hostIrqEnable();
              CHECK(IrqGate::inIsr());
              *(std::atomic<bool>*)p = true;
          },
          &nestedOk);
    });
    t.join();
    CHECK(nestedOk);
    CHECK(!IrqGate::inIsr());

    // the main loop never preempts a running ISR: it blocks until the ISR returns
    std::atomic<bool> inside{ false }, release{ false }, mainGotIn{ false };
    std::thread longIsr([&] {
        IrqGate::runIsr(
          [](void* p) {
              auto* a = (std::atomic<bool>**)p;
              *a[0] = true;
              while (!*a[1]) std::this_thread::sleep_for(1ms);
          },
          (std::atomic<bool>*[]){ &inside, &release });
    });
    while (!inside) std::this_thread::sleep_for(1ms);
    std::thread mainLike([&] {
        hostIrqDisable();
        mainGotIn = true;
        hostIrqEnable();
    });
    std::this_thread::sleep_for(100ms);
    CHECK(!mainGotIn);
    release = true;
    longIsr.join();
    mainLike.join();
    CHECK(mainGotIn);

    if (failures == 0) printf("irqgate_test OK\n");
    return failures ? 1 : 0;
}
