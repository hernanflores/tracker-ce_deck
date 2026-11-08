#ifndef HOST_PASSIVE_EVE_DEVICE_H_
#define HOST_PASSIVE_EVE_DEVICE_H_

#include "EveDevice.h"
#include "EveSpiDecoder.h"

#include <mutex>

// A chip that stores what is written to it and acknowledges the command ring immediately, without
// executing any command. Enough for the firmware to run with the display disabled and to record
// traces, but the reads that depend on co-processor results (REG_CMD_DL, image properties, ...)
// are not meaningful. Optionally paints a test pattern per display-list swap so the window path
// can be exercised.
class PassiveEveDevice : public EveDevice, protected EveSpiDecoder::Listener
{
  public:
    explicit PassiveEveDevice(bool testPattern = false) : decoder(memory, this), testPattern(testPattern) {}

    const char* name() const override { return testPattern ? "passive (test pattern)" : "passive"; }
    void select() override { decoder.select(); }
    void deselect() override { decoder.deselect(); }
    uint8_t transfer(uint8_t mosi) override { return decoder.transfer(mosi); }
    bool pollFrame(EveFrame& out) override;

    EveMemory memory;

  protected:
    void onWriteBurst(uint32_t start, uint32_t end) override;
    void onCmdWord(uint32_t, uint32_t word) override;

  private:
    void paintTestFrame();

    EveSpiDecoder decoder;
    bool testPattern;
    bool swapSeen = false;
    uint32_t swaps = 0;
    std::mutex frameMutex;
    EveFrame latest;
    bool latestFresh = false;
};

#endif
