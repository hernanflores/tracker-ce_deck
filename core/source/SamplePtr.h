#ifndef SAMPLE_PTR_H
#define SAMPLE_PTR_H

#include <assert.h>
#include <stdint.h>

// Pointer into externalRamSampleMemory stored inside file-layout structs (strInstrument).
//
// On the MCU a pointer is 4 bytes, so strInstrument is exactly 372 bytes. On a 64-bit host a raw
// pointer is 8 bytes, which would change the struct layout and break the .pti format. SamplePtr is
// therefore a plain int16_t* on the MCU and a 4-byte offset wrapper on the host.
//
// Encoding: 0 == nullptr (so zero-filled structs behave like on the MCU), otherwise
// byteOffsetFromSampleMemory + 1.

#ifdef PLATFORM_HOST

extern int16_t externalRamSampleMemory[];

class SamplePtr
{
  public:
    SamplePtr() = default;
    SamplePtr(decltype(nullptr)) : enc(0)
    {
    }
    SamplePtr(int16_t* p)
    {
        set(p);
    }
    SamplePtr& operator=(int16_t* p)
    {
        set(p);
        return *this;
    }
    operator int16_t*() const
    {
        if (enc == 0) return nullptr;
        return reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(externalRamSampleMemory) +
                                          (enc - 1));
    }

    // C-style casts such as (uint8_t*)instr.sample.address
    template <typename T> explicit operator T*() const
    {
        return reinterpret_cast<T*>(static_cast<int16_t*>(*this));
    }

  private:
    void set(int16_t* p)
    {
        if (p == nullptr)
        {
            enc = 0;
            return;
        }
        const intptr_t diff = reinterpret_cast<uint8_t*>(p) -
                              reinterpret_cast<uint8_t*>(externalRamSampleMemory);
        // Every address stored in an instrument must point into sample memory (or one past its end).
        assert(diff >= 0 && diff < 0x7FFFFFFF);
        enc = static_cast<uint32_t>(diff) + 1;
    }

    uint32_t enc;
};

static_assert(sizeof(SamplePtr) == 4, "SamplePtr must stay 4 bytes");

#else

using SamplePtr = int16_t*;

#endif

#endif /* SAMPLE_PTR_H */
