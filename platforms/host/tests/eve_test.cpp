// EveSpiDecoder / EveMemory / TraceTee / PassiveEveDevice behaviour.
#include "eve/EveSpiDecoder.h"
#include "eve/PassiveEveDevice.h"
#include "eve/TraceTee.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

using namespace eve;

static int failures = 0;
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

// bus helpers speaking the FT81x SPI protocol
static void writeBytes(EveDevice& d, uint32_t addr, const std::vector<uint8_t>& data)
{
    d.select();
    d.transfer(0x80 | (addr >> 16));
    d.transfer(addr >> 8);
    d.transfer(addr);
    for (uint8_t b : data) d.transfer(b);
    d.deselect();
}

static void write32(EveDevice& d, uint32_t addr, uint32_t v)
{
    writeBytes(d, addr, { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) });
}

static uint32_t read32(EveDevice& d, uint32_t addr)
{
    d.select();
    d.transfer(addr >> 16);
    d.transfer(addr >> 8);
    d.transfer(addr);
    d.transfer(0); // dummy
    uint32_t v = d.transfer(0);
    v |= (uint32_t)d.transfer(0) << 8;
    v |= (uint32_t)d.transfer(0) << 16;
    v |= (uint32_t)d.transfer(0) << 24;
    d.deselect();
    return v;
}

int main()
{
    // chip identity and register defaults
    {
        PassiveEveDevice dev;
        CHECK((read32(dev, kRegId) & 0xFF) == 0x7C);
        CHECK(read32(dev, kRegCmdWrite) == 0);
    }

    // read-after-write in every memory region, byte order, auto-increment
    {
        PassiveEveDevice dev;
        write32(dev, 0x000100, 0xDEADBEEF);
        CHECK(read32(dev, 0x000100) == 0xDEADBEEF);
        write32(dev, kRamDl + 4, 0x26000007);
        CHECK(read32(dev, kRamDl + 4) == 0x26000007);
        writeBytes(dev, 0x000200, { 1, 2, 3, 4, 5 });
        CHECK(read32(dev, 0x000200) == 0x04030201);
        CHECK((read32(dev, 0x000204) & 0xFF) == 5);
        CHECK(read32(dev, 0x3FFFF0) == 0); // unmapped reads as zero
    }

    // the co-processor ring is acknowledged instantly, swaps count frames
    {
        PassiveEveDevice dev;
        write32(dev, kRamCmd, 0xFFFFFF00);      // DLSTART
        write32(dev, kRamCmd + 4, 0x00000000);  // DISPLAY
        write32(dev, kRamCmd + 8, kCmdSwap);
        write32(dev, kRegCmdWrite, 12);
        CHECK(read32(dev, kRegCmdRead) == 12);
        CHECK(read32(dev, kRegFrames) == 1);

        write32(dev, kRegDlSwap, 2); // direct swap request completes immediately
        CHECK(read32(dev, kRegDlSwap) == 0);
        CHECK(read32(dev, kRegFrames) == 2);
    }

    // a burst written across the end of the command ring wraps inside it
    {
        PassiveEveDevice dev;
        writeBytes(dev, kRamCmd + kRamCmdSize - 4, { 1, 2, 3, 4, 5, 6, 7, 8 });
        CHECK(read32(dev, kRamCmd + kRamCmdSize - 4) == 0x04030201);
        CHECK(read32(dev, kRamCmd) == 0x08070605);
    }

    // host commands (3 bytes) leave memory alone and do not desynchronise the decoder
    {
        PassiveEveDevice dev;
        dev.select();
        dev.transfer(0x40);
        dev.transfer(0x00);
        dev.transfer(0x00);
        dev.deselect();
        write32(dev, 0x100, 0x11223344);
        CHECK(read32(dev, 0x100) == 0x11223344);
    }

    // trace round trip: what TraceTee records is what load() returns, and replaying it into a fresh
    // device reproduces the memory
    {
        const std::string path = "/tmp/eve_test_" + std::to_string(getpid()) + ".evetrace";
        {
            auto inner = std::make_unique<PassiveEveDevice>();
            TraceTee tee(std::move(inner), path);
            CHECK(tee.ok());
            write32(tee, 0x000400, 0xCAFEF00D);
            writeBytes(tee, kRamDl, { 9, 8, 7, 6 });
            CHECK(read32(tee, 0x000400) == 0xCAFEF00D);
        }
        std::vector<evetrace::Event> events;
        CHECK(evetrace::load(path, events));
        CHECK(events.size() == 9); // 3 transactions x (select, bytes, deselect)
        CHECK(events[0].kind == evetrace::Event::kSelect);
        CHECK(events[1].kind == evetrace::Event::kBytes && events[1].bytes.size() == 3 + 4);
        CHECK(events[2].kind == evetrace::Event::kDeselect);

        PassiveEveDevice replayed;
        for (const auto& e : events)
        {
            if (e.kind == evetrace::Event::kSelect) replayed.select();
            else if (e.kind == evetrace::Event::kDeselect) replayed.deselect();
            else for (uint8_t b : e.bytes) replayed.transfer(b);
        }
        CHECK(read32(replayed, 0x000400) == 0xCAFEF00D);
        CHECK(read32(replayed, kRamDl) == 0x06070809);
        unlink(path.c_str());

        std::vector<evetrace::Event> none;
        CHECK(!evetrace::load("/nonexistent/file", none));
    }

    if (failures == 0) printf("eve_test OK\n");
    return failures ? 1 : 0;
}
