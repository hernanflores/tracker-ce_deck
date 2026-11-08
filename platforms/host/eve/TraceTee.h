#ifndef HOST_TRACE_TEE_H_
#define HOST_TRACE_TEE_H_

#include "EveDevice.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Trace file format "EVETRC01":
//   8 bytes magic, then records:
//     'S'                      chip select asserted
//     'D'                      chip select released
//     'B' u32le len, bytes     MOSI bytes sent while selected
// Replay only needs the host-to-chip bytes: the receiving end produces its own answers.
namespace evetrace
{
constexpr char kMagic[8] = { 'E', 'V', 'E', 'T', 'R', 'C', '0', '1' };

struct Event
{
    enum Kind
    {
        kSelect,
        kDeselect,
        kBytes
    } kind;
    std::vector<uint8_t> bytes;
};

// reads a whole trace; false on a missing file or bad magic
bool load(const std::string& path, std::vector<Event>& events);
} // namespace evetrace

// Records everything that goes over the bus to a file and forwards it to the real device.
class TraceTee : public EveDevice
{
  public:
    TraceTee(std::unique_ptr<EveDevice> inner, const std::string& path);
    ~TraceTee() override;

    bool ok() const { return file != nullptr; }
    const char* name() const override { return inner->name(); }
    void select() override;
    void deselect() override;
    uint8_t transfer(uint8_t mosi) override;
    bool pollFrame(EveFrame& out) override { return inner->pollFrame(out); }

  private:
    void flushRun();

    std::unique_ptr<EveDevice> inner;
    FILE* file = nullptr;
    std::vector<uint8_t> run;
};

#endif
