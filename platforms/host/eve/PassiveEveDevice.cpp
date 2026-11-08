#include "PassiveEveDevice.h"

using namespace eve;

void PassiveEveDevice::onCmdWord(uint32_t, uint32_t word)
{
    if (word == kCmdSwap) swapSeen = true;
}

void PassiveEveDevice::onWriteBurst(uint32_t start, uint32_t end)
{
    auto touched = [&](uint32_t reg) { return start <= reg && reg < end; };

    if (touched(kRegCmdWrite))
    {
        // "execute" the ring instantly
        memory.write32(kRegCmdRead, memory.read32(kRegCmdWrite));
        if (swapSeen)
        {
            swapSeen = false;
            swaps++;
            memory.write32(kRegFrames, memory.read32(kRegFrames) + 1);
            if (testPattern) paintTestFrame();
        }
    }
    if (touched(kRegDlSwap) && memory.read32(kRegDlSwap) != 0)
    {
        memory.write32(kRegDlSwap, 0); // swap done
        memory.write32(kRegFrames, memory.read32(kRegFrames) + 1);
        if (testPattern) paintTestFrame();
    }
}

void PassiveEveDevice::paintTestFrame()
{
    EveFrame f;
    f.width = 800;
    f.height = 480;
    f.pixels.resize(800 * 480);
    const int bar = (int)((swaps * 8) % 800);
    for (int y = 0; y < 480; y++)
    {
        for (int x = 0; x < 800; x++)
        {
            const bool check = ((x / 40) + (y / 40)) & 1;
            uint32_t c = check ? 0xFF303844u : 0xFF1C2028u;
            if (x >= bar && x < bar + 8) c = 0xFFE0A030u;
            if (y < 2 || y >= 478 || x < 2 || x >= 798) c = 0xFFFFFFFFu; // 2 px frame
            f.pixels[y * 800 + x] = c;
        }
    }
    std::lock_guard<std::mutex> lock(frameMutex);
    latest = std::move(f);
    latestFresh = true;
}

bool PassiveEveDevice::pollFrame(EveFrame& out)
{
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!latestFresh) return false;
    out = latest;
    latestFresh = false;
    return true;
}
