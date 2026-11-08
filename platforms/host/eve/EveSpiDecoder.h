#ifndef HOST_EVE_SPI_DECODER_H_
#define HOST_EVE_SPI_DECODER_H_

#include <cstdint>
#include <vector>

// FT81x address map
namespace eve
{
constexpr uint32_t kRamG = 0x000000;
constexpr uint32_t kRamGSize = 1u << 20;
constexpr uint32_t kRamDl = 0x300000;
constexpr uint32_t kRamDlSize = 8u << 10;
constexpr uint32_t kRamReg = 0x302000;
constexpr uint32_t kRamRegSize = 4u << 10;
constexpr uint32_t kRamCmd = 0x308000;
constexpr uint32_t kRamCmdSize = 4u << 10;
constexpr uint32_t kRamMisc = 0x309000; // tracker / media FIFO registers
constexpr uint32_t kRamMiscSize = 4u << 10;

constexpr uint32_t kRegId = 0x302000;
constexpr uint32_t kRegFrames = 0x302004;
constexpr uint32_t kRegDlSwap = 0x302054;
constexpr uint32_t kRegCmdRead = 0x3020F8;
constexpr uint32_t kRegCmdWrite = 0x3020FC;
constexpr uint32_t kRegCmdDl = 0x302100;
constexpr uint32_t kRegCmdbSpace = 0x302574;
constexpr uint32_t kRegHsize = 0x302034;
constexpr uint32_t kRegVsize = 0x302048;
constexpr uint32_t kRegPclk = 0x302070;

constexpr uint32_t kCmdSwap = 0xFFFFFF01;
} // namespace eve

// Flat memory of the chip (RAM_G, display list, registers, command ring, misc registers).
class EveMemory
{
  public:
    EveMemory();
    void reset();

    uint8_t read8(uint32_t addr) const;
    void write8(uint32_t addr, uint8_t v);
    uint16_t read16(uint32_t addr) const;
    uint32_t read32(uint32_t addr) const;
    void write16(uint32_t addr, uint16_t v);
    void write32(uint32_t addr, uint32_t v);

    // contiguous byte access inside one region, nullptr when [addr, addr+size) is not mapped
    uint8_t* span(uint32_t addr, uint32_t size);
    const uint8_t* span(uint32_t addr, uint32_t size) const;

    std::vector<uint8_t> ramG, ramDl, ramReg, ramCmd, ramMisc;
};

// Decodes the FT81x SPI protocol (memory read/write bursts and host commands) onto an EveMemory.
class EveSpiDecoder
{
  public:
    class Listener
    {
      public:
        virtual ~Listener() = default;
        // a write burst finished: bytes [startAddr, endAddr) were written
        virtual void onWriteBurst(uint32_t startAddr, uint32_t endAddr) { (void)startAddr; (void)endAddr; }
        // a 32-bit word was completed in the command ring; `offset` is its position in RAM_CMD
        virtual void onCmdWord(uint32_t offset, uint32_t word) { (void)offset; (void)word; }
        virtual void onHostCommand(uint8_t cmd, uint8_t param) { (void)cmd; (void)param; }
    };

    EveSpiDecoder(EveMemory& memory, Listener* listener) : mem(memory), listener(listener) {}

    void select();
    void deselect();
    uint8_t transfer(uint8_t mosi);

  private:
    enum class Phase
    {
        kIdle,
        kAddr1,
        kAddr2,
        kReadDummy,
        kReadData,
        kWriteData,
        kHostParam,
        kHostPad
    };

    EveMemory& mem;
    Listener* listener;
    bool selected = false;
    Phase phase = Phase::kIdle;
    uint32_t addr = 0;
    uint32_t burstStart = 0;
    bool wrote = false;
    uint8_t hostCmd = 0;
    uint8_t hostParam = 0;
    bool isWrite = false;
};

#endif
