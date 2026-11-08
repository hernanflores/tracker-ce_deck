// Portable C implementations of the ARM DSP-extension helpers in dspinst.h, used by the desktop
// (PLATFORM_HOST) build. Every function follows the ARM Architecture Reference Manual definition
// of the instruction it replaces (saturation, rounding, wrap-around of the 32-bit result).
// Where an instruction would set the Q flag, the flag is not modelled.

#ifndef dspinst_host_h_
#define dspinst_host_h_

#include <stdint.h>

#define DSP_INLINE static inline __attribute__((always_inline, unused))

DSP_INLINE int32_t dsp_ssat(int64_t v, int bits)
{
    const int64_t max = ((int64_t)1 << (bits - 1)) - 1;
    const int64_t min = -((int64_t)1 << (bits - 1));
    return (int32_t)(v > max ? max : (v < min ? min : v));
}

DSP_INLINE int16_t dsp_lo(uint32_t v)
{
    return (int16_t)(v & 0xFFFF);
}
DSP_INLINE int16_t dsp_hi(uint32_t v)
{
    return (int16_t)(v >> 16);
}
DSP_INLINE uint32_t dsp_join(int32_t hi, int32_t lo)
{
    return ((uint32_t)(hi & 0xFFFF) << 16) | (uint32_t)(lo & 0xFFFF);
}

// computes limit((val >> rshift), 2**bits)
DSP_INLINE int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
    return dsp_ssat((int64_t)(val >> rshift), bits);
}

// computes limit(val, 2**bits)
DSP_INLINE int16_t saturate16(int32_t val)
{
    return (int16_t)dsp_ssat(val, 16);
}

// computes ((a[31:0] * b[15:0]) >> 16)
DSP_INLINE int32_t signed_multiply_32x16b(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * dsp_lo(b)) >> 16);
}

// computes ((a[31:0] * b[31:16]) >> 16)
DSP_INLINE int32_t signed_multiply_32x16t(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * dsp_hi(b)) >> 16);
}

// computes (((int64_t)a[31:0] * (int64_t)b[31:0]) >> 32)
DSP_INLINE int32_t multiply_32x32_rshift32(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 32);
}

// computes (((int64_t)a[31:0] * (int64_t)b[31:0] + 0x80000000) >> 32)
DSP_INLINE int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b)
{
    return (int32_t)((((int64_t)a * b) + 0x80000000LL) >> 32);
}

// computes sum + (((int64_t)a[31:0] * (int64_t)b[31:0] + 0x80000000) >> 32)
DSP_INLINE int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b)
{
    // SMMLAR: the accumulator is placed in the upper word of a 64-bit value, wrap-around intended
    uint64_t acc = ((uint64_t)(uint32_t)sum << 32) + (uint64_t)((int64_t)a * b) + 0x80000000ULL;
    return (int32_t)(acc >> 32);
}

// computes sum - (((int64_t)a[31:0] * (int64_t)b[31:0] + 0x80000000) >> 32)
DSP_INLINE int32_t multiply_subtract_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b)
{
    // SMMLSR
    uint64_t acc = ((uint64_t)(uint32_t)sum << 32) - (uint64_t)((int64_t)a * b) + 0x80000000ULL;
    return (int32_t)(acc >> 32);
}

// computes (a[31:16] | (b[31:16] >> 16))
DSP_INLINE uint32_t pack_16t_16t(int32_t a, int32_t b)
{
    return ((uint32_t)a & 0xFFFF0000u) | ((uint32_t)b >> 16);
}

// computes (a[31:16] | b[15:0])
DSP_INLINE uint32_t pack_16t_16b(int32_t a, int32_t b)
{
    return ((uint32_t)a & 0xFFFF0000u) | ((uint32_t)b & 0x0000FFFFu);
}

// computes ((a[15:0] << 16) | b[15:0])
DSP_INLINE uint32_t pack_16b_16b(int32_t a, int32_t b)
{
    return ((uint32_t)a << 16) | ((uint32_t)b & 0x0000FFFFu);
}

// computes (((a[31:16] + b[31:16]) << 16) | (a[15:0 + b[15:0]))  (saturates)
DSP_INLINE uint32_t signed_add_16_and_16(uint32_t a, uint32_t b)
{
    return dsp_join(dsp_ssat((int32_t)dsp_hi(a) + dsp_hi(b), 16),
                    dsp_ssat((int32_t)dsp_lo(a) + dsp_lo(b), 16));
}

// computes (((a[31:16] - b[31:16]) << 16) | (a[15:0 - b[15:0]))  (saturates)
DSP_INLINE int32_t signed_subtract_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_join(dsp_ssat((int32_t)dsp_hi(a) - dsp_hi(b), 16),
                             dsp_ssat((int32_t)dsp_lo(a) - dsp_lo(b), 16));
}

// computes out = (((a[31:16]+b[31:16])/2) <<16) | ((a[15:0]+b[15:0])/2)
DSP_INLINE int32_t signed_halving_add_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_join(((int32_t)dsp_hi(a) + dsp_hi(b)) >> 1,
                             ((int32_t)dsp_lo(a) + dsp_lo(b)) >> 1);
}

// computes out = (((a[31:16]-b[31:16])/2) <<16) | ((a[15:0]-b[15:0])/2)
DSP_INLINE int32_t signed_halving_subtract_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_join(((int32_t)dsp_hi(a) - dsp_hi(b)) >> 1,
                             ((int32_t)dsp_lo(a) - dsp_lo(b)) >> 1);
}

// computes (sum + ((a[31:0] * b[15:0]) >> 16))
DSP_INLINE int32_t signed_multiply_accumulate_32x16b(int32_t sum, int32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)sum + (uint32_t)(((int64_t)a * dsp_lo(b)) >> 16));
}

// computes (sum + ((a[31:0] * b[31:16]) >> 16))
DSP_INLINE int32_t signed_multiply_accumulate_32x16t(int32_t sum, int32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)sum + (uint32_t)(((int64_t)a * dsp_hi(b)) >> 16));
}

// computes logical and
DSP_INLINE uint32_t logical_and(uint32_t a, uint32_t b)
{
    return a & b;
}

// computes ((a[15:0] * b[15:0]) + (a[31:16] * b[31:16]))
DSP_INLINE int32_t multiply_16tx16t_add_16bx16b(uint32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)((int32_t)dsp_lo(a) * dsp_lo(b)) +
                     (uint32_t)((int32_t)dsp_hi(a) * dsp_hi(b)));
}

// computes ((a[15:0] * b[31:16]) + (a[31:16] * b[15:0]))
DSP_INLINE int32_t multiply_16tx16b_add_16bx16t(uint32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)((int32_t)dsp_lo(a) * dsp_hi(b)) +
                     (uint32_t)((int32_t)dsp_hi(a) * dsp_lo(b)));
}

// computes sum += ((a[15:0] * b[15:0]) + (a[31:16] * b[31:16]))
DSP_INLINE int64_t multiply_accumulate_16tx16t_add_16bx16b(int64_t sum, uint32_t a, uint32_t b)
{
    return (int64_t)((uint64_t)sum + (uint64_t)((int64_t)dsp_lo(a) * dsp_lo(b)) +
                     (uint64_t)((int64_t)dsp_hi(a) * dsp_hi(b)));
}

// computes sum += ((a[15:0] * b[31:16]) + (a[31:16] * b[15:0]))
DSP_INLINE int64_t multiply_accumulate_16tx16b_add_16bx16t(int64_t sum, uint32_t a, uint32_t b)
{
    return (int64_t)((uint64_t)sum + (uint64_t)((int64_t)dsp_lo(a) * dsp_hi(b)) +
                     (uint64_t)((int64_t)dsp_hi(a) * dsp_lo(b)));
}

// computes ((a[15:0] * b[15:0])
DSP_INLINE int32_t multiply_16bx16b(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_lo(a) * dsp_lo(b);
}

// computes ((a[15:0] * b[31:16])
DSP_INLINE int32_t multiply_16bx16t(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_lo(a) * dsp_hi(b);
}

// computes ((a[31:16] * b[15:0])
DSP_INLINE int32_t multiply_16tx16b(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_hi(a) * dsp_lo(b);
}

// computes ((a[31:16] * b[31:16])
DSP_INLINE int32_t multiply_16tx16t(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_hi(a) * dsp_hi(b);
}

// computes (a - b), result saturated to 32 bit integer range
DSP_INLINE int32_t substract_32_saturate(uint32_t a, uint32_t b)
{
    return dsp_ssat((int64_t)(int32_t)a - (int64_t)(int32_t)b, 32);
}

// the Q (saturation) flag is not modelled on host
DSP_INLINE uint32_t get_q_psr(void)
{
    return 0;
}

DSP_INLINE void clr_q_psr(void)
{
}

// Multiply two S.31 fractional integers, and return the 32 most significant
// bits after a shift left by the constant z.
DSP_INLINE int32_t multiply_32x32_rshift(int32_t x, int32_t y, int z)
{
    return (int32_t)(((int64_t)x * y) >> (31 - z));
}

#undef DSP_INLINE

#endif /* dspinst_host_h_ */
