#ifndef HOST_EVE_DEVICE_H_
#define HOST_EVE_DEVICE_H_

#include <cstdint>
#include <vector>

// A frame as produced by a display backend: 0xAARRGGBB pixels.
struct EveFrame
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

// The chip on the other end of the SPI bus. The firmware's FT812 library drives it with the exact
// byte stream the real FT812 sees (MCU_CSlow / MCU_SPIReadWrite / MCU_CShigh).
class EveDevice
{
  public:
    virtual ~EveDevice() = default;

    virtual const char* name() const = 0;
    virtual void select() = 0;                  // chip select asserted (low)
    virtual void deselect() = 0;                // chip select released (high)
    virtual uint8_t transfer(uint8_t mosi) = 0; // one byte on the bus, returns MISO

    // Latest completed frame since the last call (any thread). False when there is none.
    virtual bool pollFrame(EveFrame& out)
    {
        (void)out;
        return false;
    }
};

#endif
