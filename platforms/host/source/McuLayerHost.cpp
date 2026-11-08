// Host replacement of platforms/imx/3rdparty/FT812/MCU_Layer.cpp: the same MCU_* entry points, but
// chip select and bytes go to an EveDevice instead of GPIO pins and a LPSPI peripheral.
// API_Layer.cpp and EVE_Layer.cpp from the IMX port are compiled unchanged on top of this.
#include "EveHost.h"
#include "FT812.h"

#include <chrono>
#include <thread>

static EveDevice* gDevice = nullptr;

namespace evebus
{
void set(EveDevice* device)
{
    gDevice = device;
}
EveDevice* get()
{
    return gDevice;
}
} // namespace evebus

uint32_t ramDisplayList = RAM_DL;
uint32_t ramCommandBuffer = RAM_CMD;
uint16_t FT81x_GPIO_DAT;
uint16_t FT81x_GPIO_DIR;

#define FT81x_ACTIVE 0x00

static void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int FT812_HostInit(const Ft81xConfig* config)
{
    EVE_CmdWrite(FT81x_ACTIVE, 0x00); // wake the chip

    int timeout = 0;
    constexpr int kReadTimeoutMs = 1000;
    while (EVE_MemRead8(REG_ID) != 0x7C)
    {
        if (++timeout >= kReadTimeoutMs) return 1;
        sleepMs(1);
    }
    timeout = 0;
    while (EVE_MemRead8(REG_CPURESET) != 0x00)
    {
        if (++timeout >= kReadTimeoutMs) return 2;
        sleepMs(1);
    }

    // same register sequence as FT812_Init
    EVE_MemWrite8(REG_PCLK, 0);
    EVE_MemWrite8(REG_PWM_DUTY, 0);

    FT81x_GPIO_DAT = EVE_MemRead16(REG_GPIOX) | 0x8000;
    EVE_MemWrite16(REG_GPIOX, FT81x_GPIO_DAT);
    FT81x_GPIO_DIR = EVE_MemRead16(REG_GPIOX_DIR) | 0x8000;
    EVE_MemWrite16(REG_GPIOX_DIR, FT81x_GPIO_DIR);

    EVE_MemWrite16(REG_HSIZE, config->lcdWidth);
    EVE_MemWrite16(REG_HCYCLE, config->lcdHcycle);
    EVE_MemWrite16(REG_HOFFSET, config->lcdHoffset);
    EVE_MemWrite16(REG_HSYNC0, config->lcdHsync0);
    EVE_MemWrite16(REG_HSYNC1, config->lcdHsync1);
    EVE_MemWrite16(REG_VSIZE, config->lcdHeight);
    EVE_MemWrite16(REG_VCYCLE, config->lcdVcycle);
    EVE_MemWrite16(REG_VOFFSET, config->lcdVoffset);
    EVE_MemWrite16(REG_VSYNC0, config->lcdVsync0);
    EVE_MemWrite16(REG_VSYNC1, config->lcdVsync1);

    EVE_MemWrite8(REG_SWIZZLE, config->lcdSwizzle);
    EVE_MemWrite8(REG_PCLK_POL, config->lcdPclkpol);
    EVE_MemWrite8(REG_CSPREAD, config->lcdCsSpread);
    EVE_MemWrite8(REG_ROTATE, config->lcdRotate);
    EVE_MemWrite8(REG_DITHER, config->lcdDither);
    EVE_MemWrite16(REG_OUTBITS, config->outBits);

    ramDisplayList = RAM_DL;
    EVE_MemWrite32(ramDisplayList, 0x02000000); // CLEAR_COLOR_RGB
    ramDisplayList += 4;
    EVE_MemWrite32(ramDisplayList, 0x26000000 | 0x00000007); // CLEAR
    ramDisplayList += 4;
    EVE_MemWrite32(ramDisplayList, 0x00000000); // DISPLAY
    EVE_MemWrite32(REG_DLSWAP, DLSWAP_FRAME);

    EVE_MemWrite16(REG_TOUCH_RZTHRESH, 1200);
    EVE_MemWrite8(REG_VOL_PB, 0);
    EVE_MemWrite8(REG_VOL_SOUND, 0);
    EVE_MemWrite16(REG_SOUND, 0x6000);

    EVE_MemWrite8(REG_PCLK, config->lcdPclk);
    EVE_MemWrite16(REG_PWM_HZ, 250);
    return 0;
}

void FT812_SetFastTransferMode()
{
}

void MCU_CSlow(void)
{
    if (gDevice) gDevice->select();
}

void MCU_CShigh(void)
{
    if (gDevice) gDevice->deselect();
}

void MCU_PDlow(void)
{
}

void MCU_PDhigh(void)
{
}

uint8_t MCU_SPIReadWrite(uint8_t DataToWrite)
{
    return gDevice ? gDevice->transfer(DataToWrite) : 0;
}

// SpiBus::transfer4Bytes sends the least significant byte first
void MCU_SPIWrite32(uint32_t DataToWrite)
{
    if (!gDevice) return;
    gDevice->transfer((uint8_t)(DataToWrite));
    gDevice->transfer((uint8_t)(DataToWrite >> 8));
    gDevice->transfer((uint8_t)(DataToWrite >> 16));
    gDevice->transfer((uint8_t)(DataToWrite >> 24));
}

void MCU_Delay_3us(void)
{
}

void MCU_Delay_20ms(void)
{
    sleepMs(20);
}

void MCU_Delay_500ms(void)
{
    sleepMs(500);
}

void MCU_set_sleepMode(void)
{
    EVE_MemWrite8(REG_PWM_DUTY, 0);
    FT81x_GPIO_DAT = EVE_MemRead16(REG_GPIOX) & (~0x8000);
    EVE_MemWrite16(REG_GPIOX, FT81x_GPIO_DAT);
}

void MCU_set_runMode(void)
{
    EVE_MemWrite8(REG_PWM_DUTY, 64);
}
