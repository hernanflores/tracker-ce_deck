#include "EveSpiDecoder.h"

#include <cstring>

using namespace eve;

EveMemory::EveMemory()
{
    ramG.assign(kRamGSize, 0);
    ramDl.assign(kRamDlSize, 0);
    ramReg.assign(kRamRegSize, 0);
    ramCmd.assign(kRamCmdSize, 0);
    ramMisc.assign(kRamMiscSize, 0);
    reset();
}

void EveMemory::reset()
{
    std::fill(ramG.begin(), ramG.end(), 0);
    std::fill(ramDl.begin(), ramDl.end(), 0);
    std::fill(ramReg.begin(), ramReg.end(), 0);
    std::fill(ramCmd.begin(), ramCmd.end(), 0);
    std::fill(ramMisc.begin(), ramMisc.end(), 0);
    write32(kRegId, 0x7C);           // chip id
    write32(kRegCmdbSpace, kRamCmdSize - 4);
}

uint8_t* EveMemory::span(uint32_t a, uint32_t size)
{
    auto in = [&](uint32_t base, uint32_t len, std::vector<uint8_t>& v) -> uint8_t* {
        return (a >= base && a + size <= base + len) ? v.data() + (a - base) : nullptr;
    };
    if (uint8_t* p = in(kRamG, kRamGSize, ramG)) return p;
    if (uint8_t* p = in(kRamDl, kRamDlSize, ramDl)) return p;
    if (uint8_t* p = in(kRamReg, kRamRegSize, ramReg)) return p;
    if (uint8_t* p = in(kRamCmd, kRamCmdSize, ramCmd)) return p;
    if (uint8_t* p = in(kRamMisc, kRamMiscSize, ramMisc)) return p;
    return nullptr;
}

const uint8_t* EveMemory::span(uint32_t a, uint32_t size) const
{
    return const_cast<EveMemory*>(this)->span(a, size);
}

uint8_t EveMemory::read8(uint32_t a) const
{
    const uint8_t* p = span(a, 1);
    return p ? *p : 0;
}

void EveMemory::write8(uint32_t a, uint8_t v)
{
    if (uint8_t* p = span(a, 1)) *p = v;
}

uint16_t EveMemory::read16(uint32_t a) const
{
    return (uint16_t)(read8(a) | (read8(a + 1) << 8));
}

uint32_t EveMemory::read32(uint32_t a) const
{
    return (uint32_t)read16(a) | ((uint32_t)read16(a + 2) << 16);
}

void EveMemory::write16(uint32_t a, uint16_t v)
{
    write8(a, (uint8_t)v);
    write8(a + 1, (uint8_t)(v >> 8));
}

void EveMemory::write32(uint32_t a, uint32_t v)
{
    write16(a, (uint16_t)v);
    write16(a + 2, (uint16_t)(v >> 16));
}

//----------------------------------------------------------------------------------------------
void EveSpiDecoder::select()
{
    selected = true;
    phase = Phase::kIdle;
    wrote = false;
}

void EveSpiDecoder::deselect()
{
    if (selected && wrote && listener != nullptr)
    {
        listener->onWriteBurst(burstStart, addr);
    }
    selected = false;
    phase = Phase::kIdle;
    wrote = false;
}

uint8_t EveSpiDecoder::transfer(uint8_t mosi)
{
    if (!selected) return 0;

    switch (phase)
    {
        case Phase::kIdle:
            switch (mosi >> 6)
            {
                case 0: // memory read
                case 2: // memory write
                    isWrite = (mosi >> 6) == 2;
                    addr = (uint32_t)(mosi & 0x3F) << 16;
                    phase = Phase::kAddr1;
                    break;
                case 1: // host command
                    hostCmd = mosi & 0x3F;
                    phase = Phase::kHostParam;
                    break;
                default: break; // reserved
            }
            return 0;

        case Phase::kAddr1:
            addr |= (uint32_t)mosi << 8;
            phase = Phase::kAddr2;
            return 0;

        case Phase::kAddr2:
            addr |= mosi;
            burstStart = addr;
            phase = isWrite ? Phase::kWriteData : Phase::kReadDummy;
            return 0;

        case Phase::kReadDummy:
            phase = Phase::kReadData;
            return 0;

        case Phase::kReadData:
        {
            const uint8_t v = mem.read8(addr);
            addr++;
            return v;
        }

        case Phase::kWriteData:
        {
            mem.write8(addr, mosi);
            wrote = true;
            if (addr >= kRamCmd && addr < kRamCmd + kRamCmdSize && (addr & 3) == 3 && listener)
            {
                const uint32_t off = addr - kRamCmd - 3;
                listener->onCmdWord(off, mem.read32(addr - 3));
            }
            addr++;
            // the command ring wraps inside its 4 KB window
            if (addr == kRamCmd + kRamCmdSize && burstStart >= kRamCmd) addr = kRamCmd;
            return 0;
        }

        case Phase::kHostParam:
            hostParam = mosi;
            phase = Phase::kHostPad;
            return 0;

        case Phase::kHostPad:
            if (listener) listener->onHostCommand(hostCmd, hostParam);
            phase = Phase::kIdle;
            return 0;
    }
    return 0;
}
