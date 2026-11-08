#ifndef HOST_PLATFORM_H_
#define HOST_PLATFORM_H_

// Desktop implementation of the abstract Platform interface (core/include/Platform.h).

#include "ConfigManager.h"
#include "DisplayDriver.h"
#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// Paths shared with the device firmware (platforms/imx/config/config.h)
static constexpr char kConfigFilePath[] = "/trackerConfig";
static constexpr char kFirmwareInfoFilePath[] = "/Firmware/_fwinfo";
static constexpr char kTurnOnFilePath[] = "/Firmware/_turnon";

//----------------------------------------------------------------------------------------------
// Interrupt emulation. See IrqGate.cpp.
//----------------------------------------------------------------------------------------------
namespace IrqGate
{
// Audio thread: runs `isr` the way the audio DMA interrupt would run on the MCU.
void runIsr(void (*isr)(void*), void* arg);
bool inIsr();
// longest time the audio thread waited for the gate (the main thread had interrupts disabled)
uint32_t maxWaitUs();
void resetStats();
} // namespace IrqGate

//----------------------------------------------------------------------------------------------
class TimerHost final : public Timer
{
  public:
    TimerHost();
    uint32_t millis() override;
    uint32_t micros() override;
    void delay(int ms) override;
    uint32_t clockCycles() override;
    uint32_t clockCyclesPerUs() override;

  private:
    std::chrono::steady_clock::time_point start;
};

//----------------------------------------------------------------------------------------------
// Button group. Events are posted from any thread and delivered from the main loop (update()),
// like Keys::update on the device, including the progressive hold/auto-repeat timing.
//----------------------------------------------------------------------------------------------
class ButtonsHost final : public SwitchGroup
{
  public:
    static constexpr int kMaxButtons = 64;

    explicit ButtonsHost(Timer* timer);
    void setHoldTime(int timeMs) override;
    bool isButtonPressed(int buttonIdx) override;
    void setActionCallback(ActionType action, void (*callback)(uint8_t)) override;

    // thread safe
    void postEvent(int buttonIdx, bool pressed);
    // main thread only
    void update();

  private:
    struct Event
    {
        uint8_t idx;
        bool pressed;
    };

    Timer* timer;
    void (*callbacks[3])(uint8_t) = { nullptr, nullptr, nullptr };
    uint16_t holdTime = 333;
    bool pressedState[kMaxButtons] = {};
    uint32_t holdStartMs[kMaxButtons] = {};
    uint8_t holdFactor[kMaxButtons] = {};
    std::mutex queueMutex;
    std::vector<Event> queue;
    std::atomic<bool> pressedAtomic[kMaxButtons];
};

class SilentSwitchGroup final : public SwitchGroup
{
  public:
    void setHoldTime(int) override {}
    bool isButtonPressed(int) override { return false; }
    void setActionCallback(ActionType, void (*)(uint8_t)) override {}
};

class LedsHost final : public Leds
{
  public:
    void setLed(int, bool, int) override {}
};

class EncoderHost final : public Encoder
{
  public:
    void setAcceleration(int) override {}
    void setResolution(int) override {}
};

class BatteryHost final : public Battery
{
  public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return 4.2f; }
    bool getCharging() override { return false; }
    int getTimeTo() override { return 0; }
    bool getState() override { return true; }
    bool isEmpty() override { return false; }
};

class RadioHost final : public Radio
{
  public:
    void enableLineInput(bool) override {}
    void enable(bool) override {}
    bool setFrequency(float) override { return false; }
    void setSeekCallback(void (*)(void)) override {}
    void seek(SeekDirection) override {}
    void updateSeek() override {}
    bool updateRDS() override { return false; }
    float getFrequency() override { return 0; }
    float getBaseFrequency() override { return 87.5f; }
    float getSpacing() override { return 0.1f; }
    bool isinitialized() override { return false; }
    void clearRDS() override {}
    void setRegion(Region) override {}
    bool isRegionChanged() override { return false; }
    char* getStationName() override { return name; }

  private:
    char name[9] = {};
};

// Codec settings are stored; the audio backend applies master volume in software.
class Sgtl5000Host final : public Sgtl5000
{
  public:
    void init() override {}
    void setInputType(InputType input) override { inputType = input; }
    void setHeadphoneSource(HeadphoneSource) override {}
    void setLineInLevel(int) override {}
    void setMicGain(int gainDb) override { micGainDb = gainDb; }
    void setHeadphoneMuted(bool muted) override { headphoneMuted = muted; }
    void setMasterVolume(float volume) override { masterVolume = volume; }
    void setAutoVolumeEnabled(bool) override {}
    void configureAutoVolume(AutoVolumeParams*) override {}
    void setEnhanceBassEnabled(bool) override {}
    void configureEnhanceBass(EnhanceBassParams*) override {}
    void setEqualizerType(EqualizerType) override {}
    void configureFiveBandEqualizer(float*) override {}
    void enableSurroundSound(bool) override {}
    void configureSurroundSound(SurroundSoundParams*) override {}
    void enableHardwareProcessor(bool) override {}

    std::atomic<float> masterVolume{ 1.0f };
    std::atomic<bool> headphoneMuted{ false };
    std::atomic<int> micGainDb{ 0 };
    std::atomic<int> inputType{ kLineIn };
};

class UartMidiHost final : public UartMidi
{
  public:
    void init() override {}
    void setReceiveActions(MidiReceiveActions*) override {}
    void setReceiveNoteOnAction(void (*)(uint8_t, uint8_t, uint8_t)) override {}
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t, uint8_t, uint8_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendAfterTouch(uint8_t, uint8_t) override {}
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override {}
    void sendPitchBend(int, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
};

class UsbMidiHost final : public UsbMidi
{
  public:
    void setReceiveActions(MidiReceiveActions*) override {}
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t, uint8_t, uint8_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendAfterTouch(uint8_t, uint8_t) override {}
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override {}
    void sendPitchBend(int, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
};

class UsbAudioHost final : public UsbAudio
{
  public:
    void beginFillData() override {}
    void fillChannelData(void*, int, int) override {}
    void finishFillData() override {}
};

class UsbHost final : public Usb
{
  public:
    void init() override {}
    void reset() override {}
    void update() override {}
    void setClassState(ClassType, bool) override {}
    bool getClassState(ClassType) override { return false; }
    bool isAttached() override { return false; }
};

// Same as the device: CRC checking is disabled, so cards stay interchangeable.
class CrcCalculatorHost final : public CrcCalculator
{
  public:
    uint32_t calculate32bitCrc(const uint8_t*, const uint16_t) override { return 0; }
};

// Display driver that discards everything (--display=null).
class DisplayDriverNull final : public DisplayDriver
{
  public:
    void setPowerState(bool) override {}
    void setBrightness(int) override {}
    void setRotation(int) override {}
    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override
    {
        addr = 0;
        width = 800;
        height = 480;
    }
    uint32_t getDisplayListOffset() override { return 0; }
    void IncCMDOffset(uint16_t) override {}
    void WriteData32(uint32_t) override {}
    void beginCoProList() override {}
    void beginCoProListNoCheck() override {}
    void endCoProList() override {}
    void awaitCoProEmpty() override {}
    uint8_t isCoProEmpty() override { return 1; }
    void writeDataRAMG(const uint8_t*, uint32_t, uint32_t) override {}
    void writeFontRAMG(const uint8_t*, uint32_t, uint32_t) override {}
    void readDataRAMG(uint8_t* data, uint32_t size, uint32_t) override { memset(data, 0, size); }
    void readBackDataRAMG(uint8_t* data, uint32_t size, uint32_t) override
    {
        memset(data, 0, size);
    }
    uint8_t sendString(const char*, uint32_t) override { return 1; }
    void writeDataToCMD(const uint8_t*, uint32_t) override {}
    void initStreamDma(uint16_t, uint16_t, uint16_t, uint8_t*, uint32_t) override {}
    bool updateStreamDma(bool) override { return false; }
    void stopStreamDma() override {}
};

//----------------------------------------------------------------------------------------------
// Config stored as /trackerConfig on the SD root, same as the device.
//----------------------------------------------------------------------------------------------
class ConfigManagerHost final : public ConfigManager
{
  public:
    void init(FileSystem* fs) { fileSystem = fs; }
    bool readData(int address, void* data, int size) override;
    void writeData(int address, void* data, int size) override;

  private:
    FileSystem* fileSystem = nullptr;
};

//----------------------------------------------------------------------------------------------
// FileSystem on a host directory that plays the role of the SD card root.
//----------------------------------------------------------------------------------------------
struct HostFile;

class FileSystemHost final : public FileSystem
{
  public:
    explicit FileSystemHost(std::string rootDir);
    ~FileSystemHost();

    void init();
    void update() {}
    const std::string& root() const { return rootPath; }

    bool isSdCardInserted() override { return true; }
    void setMountEventCallback(CardStateChangeCallback funct) override { mountCallback = funct; }
    bool remount() override { return true; }

    bool exists(const char* path) override;
    bool rename(const char* oldPath, const char* newPath) override;
    bool mkdir(uint8_t hidden, const char* path, bool pFlag = true) override;
    bool remove(const char* path) override;
    bool removeDirWithFiles(const char* path) override;
    bool removeOnlyDirFiles(const char* path) override;

    size_t getFileSize(const char* path) override;
    size_t getFileSize(FileHandle fileHandle) override;
    size_t getFileAvailable(FileHandle fileHandle) override;

    std::tuple<bool, FileHandle> openFile(const char* filepath,
                                          FileOpenMode openMode = kRead,
                                          FileHandle* fileHandle = nullptr) override;
    int32_t readFile(void* destBuf, size_t bufLen, FileHandle file) override;
    int32_t writeFile(const void* data, size_t dataSize, FileHandle fileHandle) override;
    bool seekFileCur(int offset, FileHandle fileHandle) override;
    bool seekFile(uint32_t pos, FileHandle fileHandle) override;
    void closeFile(FileHandle file, bool deleteOnClose = true) override;
    void endFile(FileHandle file) override;
    bool isFileOpen(FileHandle file) override;
    // No cluster maps on a host file system: streaming falls back to synchronous reads.
    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t* = nullptr, uint32_t* = nullptr) override
    {
        return false;
    }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool* outSuccess = nullptr) override
    {
        if (outSuccess) *outSuccess = false;
        return true;
    }
    bool waitAsyncRead() override { return true; }
    bool isAsyncReadBusy() override { return false; }

    int createInstrumentsList(const char* path,
                              char** list,
                              uint8_t list_length,
                              uint32_t* sizeOut) override;
    int countContainedFiles(const char* path) override;
    int createFilesList(const char* path,
                        uint8_t start_line,
                        char** list,
                        uint8_t list_length,
                        uint16_t max_used_memory,
                        enListFileFilter filter = enListFileFilter_all,
                        uint16_t skip_count = 0,
                        uint16_t max_files_in_subdir = 0,
                        enListFileFilter folderSizeFilter = enListFileFilter_all) override;
    void listDirContents(const char* dirpath,
                         DirContentType contentType,
                         std::function<bool(const char* name)> nameFilter,
                         char* namesBuffer,
                         int namesBufferSize,
                         int maxNameLen,
                         int& outNamesCount,
                         char** fileList) override;
    int readNbFiles(const char* romPath) override;
    bool nesDir_open(const char* path) override;
    bool nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override;
    void nesDir_close() override;

    // Case-insensitive resolution of a card path (FAT is case-insensitive) into a host path.
    std::string resolve(const char* cardPath) const;

    struct Entry
    {
        std::string name;
        bool isDir;
        bool hidden;
        uint64_t size;
    };
    // Directory content in a stable (case-insensitive name) order, "." and ".." excluded.
    std::vector<Entry> readDir(const std::string& hostPath) const;

  private:
    std::string rootPath;
    CardStateChangeCallback mountCallback = nullptr;
    std::vector<Entry> nesEntries;
    size_t nesPos = 0;
};

//----------------------------------------------------------------------------------------------
class PlatformHost final : public Platform
{
  public:
    PlatformHost(const std::string& sdRoot, bool nullDisplay);

    void init();
    void update();

    // Set when the firmware asks to power off / reset. The main loop acts on them.
    std::atomic<bool> quitRequested{ false };
    std::atomic<bool> restartRequested{ false };

    void setPowerMode(bool mode) override;
    void resetMCU() override;
    void runBootloader(char* fileName) override;
    void setPowerButtonCallback(PowerButtonCallback cb) override { powerButtonCallback = cb; }
    bool getPowerButtonState() override { return true; } // always "on" so the Mini boot gate passes
    bool readForceStartUp() override;

    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return &buttons; }
    SwitchGroup* getPads() override { return &pads; }
    Leds* getLeds() override { return &leds; }
    Battery* getBattery() override { return &battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return &encoder; }
    DisplayDriver* getDisplayDriver() override { return displayDriver; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uartMidi; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return &configManager; }
    FileSystem* getFileSystem() override { return &fileSystem; }
    CrcCalculator* getCrcCalculator() override { return &crcCalculator; }
    SystemInfo* getSystemInfo() override { return &systemInfo; }
    Usb* getUsb() override { return &usb; }

    // Host-side access
    ButtonsHost& hostButtons() { return buttons; }
    Sgtl5000Host& hostCodec() { return codec; }
    FileSystemHost& hostFileSystem() { return fileSystem; }
    void setDisplayDriver(DisplayDriver* driver) { displayDriver = driver; }

  private:
    TimerHost timer;
    FileSystemHost fileSystem;
    ConfigManagerHost configManager;
    ButtonsHost buttons;
    SilentSwitchGroup pads;
    LedsHost leds;
    EncoderHost encoder;
    BatteryHost battery;
    RadioHost radio;
    Sgtl5000Host codec;
    UartMidiHost uartMidi;
    UsbMidiHost usbMidi;
    UsbAudioHost usbAudio;
    UsbHost usb;
    CrcCalculatorHost crcCalculator;
    DisplayDriverNull nullDriver;
    DisplayDriver* displayDriver;
    PowerButtonCallback powerButtonCallback = nullptr;
    static SystemInfo systemInfo;
};

// Audio engine hooks implemented by the audio backend / main
extern std::atomic<bool> gAudioOutMuted;

#endif /* HOST_PLATFORM_H_ */
