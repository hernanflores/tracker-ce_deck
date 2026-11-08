// Host version of platforms/imx/source/DisplayDriverImpl.cpp. Same calls into the FT812 library;
// the pin setup and the LPSPI DMA transfers are gone (the stream is copied synchronously).
#include "EveHost.h"
#include "FT812.h"

#include <string.h>

bool DisplayDriverHost::init(EveDevice* device)
{
    evebus::set(device);
    if (FT812_HostInit(&kConfig800_480) != 0) return false;
    FT812_setCMDOffsetPtr(&cmdOffset);
    return true;
}

void DisplayDriverHost::setPowerState(bool state)
{
    if (state)
        MCU_set_runMode();
    else
        MCU_set_sleepMode();
}

void DisplayDriverHost::setBrightness(int value)
{
    EVE_MemWrite8(REG_PWM_DUTY, value);
}

void DisplayDriverHost::setRotation(int value)
{
    EVE_MemWrite8(REG_ROTATE, value);
}

void DisplayDriverHost::getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height)
{
    uint16_t Reg_Cmd_Write_Offset = EVE_MemRead16(REG_CMD_WRITE);

    uint32_t ParameterAddr = ((Reg_Cmd_Write_Offset - 12) & 4095);
    addr = EVE_MemRead32((RAM_CMD + ParameterAddr));
    ParameterAddr = ((Reg_Cmd_Write_Offset - 8) & 4095);
    width = EVE_MemRead32((RAM_CMD + ParameterAddr));
    ParameterAddr = ((Reg_Cmd_Write_Offset - 4) & 4095);
    height = EVE_MemRead32((RAM_CMD + ParameterAddr));
}

uint32_t DisplayDriverHost::getDisplayListOffset()
{
    return EVE_MemRead32(REG_CMD_DL);
}

void DisplayDriverHost::IncCMDOffset(uint16_t value)
{
    cmdOffset = EVE_IncCMDOffset(cmdOffset, value);
}

void DisplayDriverHost::WriteData32(uint32_t data)
{
    EVE_Write32(data);
}

void DisplayDriverHost::beginCoProList()
{
    API_LIB_BeginCoProList();
}

void DisplayDriverHost::beginCoProListNoCheck()
{
    API_LIB_BeginCoProListNoCheck();
}

void DisplayDriverHost::endCoProList()
{
    API_LIB_EndCoProList();
}

void DisplayDriverHost::awaitCoProEmpty()
{
    API_LIB_AwaitCoProEmpty();
}

uint8_t DisplayDriverHost::isCoProEmpty()
{
    return API_LIB_IsCoProEmpty();
}

void DisplayDriverHost::writeDataRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress)
{
    API_LIB_WriteDataRAMG(imgData, dataSize, destAddress);
}

void DisplayDriverHost::writeFontRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress)
{
    API_LIB_WriteFontRAMG(imgData, dataSize, destAddress);
}

void DisplayDriverHost::readDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress)
{
    API_LIB_ReadDataRAMG(imgData, dataSize, sourceAddress);
}

void DisplayDriverHost::readBackDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress)
{
    API_LIB_ReadBackDataRAMG(imgData, dataSize, sourceAddress);
}

uint8_t DisplayDriverHost::sendString(const char* string, uint32_t length)
{
    return API_SendString(string, length);
}

void DisplayDriverHost::writeDataToCMD(const uint8_t* imgData, uint32_t totalDataSize)
{
    API_LIB_WriteDataToCMD(imgData, totalDataSize);
}

//----------------------------------------------------------------------------------------------
// Frame streaming (core/libs/NewDisplay/stream.cpp): the device DMAs a frame buffer into RAM_G
// once the frame is complete. On the host that is a plain burst write.
//----------------------------------------------------------------------------------------------
void DisplayDriverHost::initStreamDma(uint16_t byteResolution,
                                      uint16_t streamWidth,
                                      uint16_t streamHeight,
                                      uint8_t* streamBuf,
                                      uint32_t destMemAddress)
{
    if (streamInitialized) return;
    streamDest = destMemAddress;
    streamSize = (uint32_t)byteResolution * streamWidth * streamHeight;
    streamBuffer = streamBuf;
    streamInitialized = true;
}

bool DisplayDriverHost::updateStreamDma(bool lastFrame)
{
    if (!streamInitialized) return true;
    if (!lastFrame) return false;

    MCU_CSlow();
    EVE_AddrForWr(streamDest);
    for (uint32_t i = 0; i < streamSize; i++) MCU_SPIReadWrite(streamBuffer[i]);
    MCU_CShigh();
    return true;
}

void DisplayDriverHost::stopStreamDma()
{
    streamInitialized = false;
}
