#ifndef HOST_EVE_HOST_H_
#define HOST_EVE_HOST_H_

#include "DisplayDriver.h"
#include "eve/EveDevice.h"

// Bus the firmware's FT812 library (MCU_CSlow, MCU_SPIReadWrite, ...) talks to.
namespace evebus
{
void set(EveDevice* device);
EveDevice* get();
} // namespace evebus

struct Ft81xConfig;
// Power-up and panel configuration sequence of FT812_Init without the GPIO pins. 0 on success.
int FT812_HostInit(const Ft81xConfig* config);

class DisplayDriverHost final : public DisplayDriver
{
  public:
    // returns false when the chip does not answer
    bool init(EveDevice* device);

    void setPowerState(bool state) override;
    void setBrightness(int value) override;
    void setRotation(int value) override;
    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override;
    uint32_t getDisplayListOffset() override;
    void IncCMDOffset(uint16_t value) override;
    void WriteData32(uint32_t data) override;
    void beginCoProList() override;
    void beginCoProListNoCheck() override;
    void endCoProList() override;
    void awaitCoProEmpty() override;
    uint8_t isCoProEmpty() override;
    void writeDataRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress) override;
    void writeFontRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress) override;
    void readDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress) override;
    void readBackDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress) override;
    uint8_t sendString(const char* string, uint32_t length) override;
    void writeDataToCMD(const uint8_t* imgData, uint32_t totalDataSize) override;
    void initStreamDma(uint16_t byteResolution,
                       uint16_t streamWidth,
                       uint16_t streamHeight,
                       uint8_t* streamBuf,
                       uint32_t destMemAddress) override;
    bool updateStreamDma(bool lastFrame) override;
    void stopStreamDma() override;

  private:
    uint16_t cmdOffset = 0;
    bool streamInitialized = false;
    uint8_t* streamBuffer = nullptr;
    uint32_t streamSize = 0;
    uint32_t streamDest = 0;
};

#endif
