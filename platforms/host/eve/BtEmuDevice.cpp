// Backend that runs the FT812 command stream through Bridgetek's emulator. Compiled only when
// BT8XXEMU_INCLUDE_DIR is set (see CMakeLists.txt); the header and library are Bridgetek's
// proprietary files and are never part of this repository.
#include "BtEmuDevice.h"

#include "EveSpiDecoder.h"

#include <bt8xxemu.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <mutex>

namespace
{
using DefaultsFn = void (*)(uint32_t, BT8XXEMU_EmulatorParameters*, BT8XXEMU_EmulatorMode);
using RunFn = void (*)(uint32_t, BT8XXEMU_Emulator**, const BT8XXEMU_EmulatorParameters*);
using StopFn = void (*)(BT8XXEMU_Emulator*);
using DestroyFn = void (*)(BT8XXEMU_Emulator*);
using TransferFn = uint8_t (*)(BT8XXEMU_Emulator*, uint8_t);
using ChipSelectFn = void (*)(BT8XXEMU_Emulator*, int);

constexpr uint32_t kRegRotate = 0x302058;

class BtEmuDevice final : public EveDevice, private EveSpiDecoder::Listener
{
  public:
    BtEmuDevice() : snoop(snoopMemory, this) {}

    ~BtEmuDevice() override
    {
        if (emulator != nullptr)
        {
            stopFn(emulator);
            destroyFn(emulator);
        }
        if (lib != nullptr) dlclose(lib);
    }

    bool open(const std::string& path, std::string& error)
    {
        lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib == nullptr)
        {
            error = std::string("cannot load ") + path + ": " + dlerror();
            return false;
        }
        auto sym = [&](const char* n) { return dlsym(lib, n); };
        defaultsFn = (DefaultsFn)sym("BT8XXEMU_defaults");
        runFn = (RunFn)sym("BT8XXEMU_run");
        stopFn = (StopFn)sym("BT8XXEMU_stop");
        destroyFn = (DestroyFn)sym("BT8XXEMU_destroy");
        transferFn = (TransferFn)sym("BT8XXEMU_transfer");
        chipSelectFn = (ChipSelectFn)sym("BT8XXEMU_chipSelect");
        if (!defaultsFn || !runFn || !stopFn || !destroyFn || !transferFn || !chipSelectFn)
        {
            error = "emulator library lacks the expected BT8XXEMU_* entry points";
            return false;
        }

        BT8XXEMU_EmulatorParameters params;
        memset(&params, 0, sizeof(params));
        defaultsFn(BT8XXEMU_VERSION_API, &params, BT8XXEMU_EmulatorFT812);
        // No window, audio, keyboard or mouse: frames come through the Graphics callback.
        params.Flags &= ~(BT8XXEMU_EmulatorEnableKeyboard | BT8XXEMU_EmulatorEnableAudio |
                          BT8XXEMU_EmulatorEnableMouse | BT8XXEMU_EmulatorEnableDebugShortkeys |
                          BT8XXEMU_EmulatorEnableDynamicDegrade);
        params.Flags |= BT8XXEMU_EmulatorEnableCoprocessor;
        params.Graphics = &BtEmuDevice::graphicsCallback;
        params.UserContext = this;

        runFn(BT8XXEMU_VERSION_API, &emulator, &params);
        if (emulator == nullptr)
        {
            error = "BT8XXEMU_run failed";
            return false;
        }
        return true;
    }

    const char* name() const override { return "Bridgetek emulator"; }

    void select() override
    {
        snoop.select();
        chipSelectFn(emulator, 1);
    }

    void deselect() override
    {
        snoop.deselect();
        chipSelectFn(emulator, 0);
    }

    uint8_t transfer(uint8_t mosi) override
    {
        snoop.transfer(mosi);
        return transferFn(emulator, mosi);
    }

    bool pollFrame(EveFrame& out) override
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!fresh) return false;
        out = std::move(latest);
        fresh = false;
        return true;
    }

  private:
    // The firmware sets REG_ROTATE for its upside-down panel; the Graphics callback delivers the
    // panel output with that rotation applied, so undo it to get the upright picture.
    void onWriteBurst(uint32_t start, uint32_t end) override
    {
        if (start <= kRegRotate && kRegRotate < end) rotate180 = snoopMemory.read8(kRegRotate) & 1;
    }

    static int graphicsCallback(BT8XXEMU_Emulator*,
                                void* context,
                                int output,
                                const argb8888* buffer,
                                uint32_t hsize,
                                uint32_t vsize,
                                BT8XXEMU_FrameFlags flags)
    {
        auto* self = (BtEmuDevice*)context;
        if (output && buffer != nullptr && (flags & BT8XXEMU_FrameBufferComplete))
        {
            EveFrame f;
            f.width = (int)hsize;
            f.height = (int)vsize;
            f.pixels.resize((size_t)hsize * vsize);
            if (self->rotate180)
            {
                for (size_t i = 0; i < f.pixels.size(); i++) f.pixels[i] = buffer[f.pixels.size() - 1 - i];
            }
            else
            {
                memcpy(f.pixels.data(), buffer, f.pixels.size() * sizeof(uint32_t));
            }
            std::lock_guard<std::mutex> lock(self->frameMutex);
            self->latest = std::move(f);
            self->fresh = true;
        }
        return 1; // keep running
    }

    void* lib = nullptr;
    BT8XXEMU_Emulator* emulator = nullptr;
    DefaultsFn defaultsFn = nullptr;
    RunFn runFn = nullptr;
    StopFn stopFn = nullptr;
    DestroyFn destroyFn = nullptr;
    TransferFn transferFn = nullptr;
    ChipSelectFn chipSelectFn = nullptr;

    EveMemory snoopMemory;
    EveSpiDecoder snoop;
    volatile bool rotate180 = false;

    std::mutex frameMutex;
    EveFrame latest;
    bool fresh = false;
};
} // namespace

std::unique_ptr<EveDevice> makeBtEmuDevice(const std::string& libraryPath, std::string& error)
{
    auto dev = std::make_unique<BtEmuDevice>();
    if (!dev->open(libraryPath, error)) return nullptr;
    return dev;
}
