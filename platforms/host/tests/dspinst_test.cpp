// Edge cases of the portable DSP helpers against the ARM instruction definitions.
#define PLATFORM_HOST 1
#include "dspinst.h"

#include <cstdint>
#include <cstdio>

static int failures = 0;
#define EXPECT(expr, value)                                                                        \
    do                                                                                             \
    {                                                                                              \
        long long got = (long long)(expr);                                                         \
        long long want = (long long)(value);                                                       \
        if (got != want)                                                                           \
        {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s = %lld, expected %lld\n", __LINE__, #expr, got, want);\
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main()
{
    // SSAT #16 with arithmetic shift
    EXPECT(saturate16(40000), 32767);
    EXPECT(saturate16(-40000), -32768);
    EXPECT(saturate16(123), 123);
    EXPECT(saturate16(INT32_MIN), -32768);
    EXPECT(signed_saturate_rshift(0x7FFFFFFF, 16, 4), 32767);
    EXPECT(signed_saturate_rshift(-0x7FFFFFFF, 16, 4), -32768);
    EXPECT(signed_saturate_rshift(0x10000, 16, 4), 4096);
    EXPECT(signed_saturate_rshift(-16, 8, 4), -1);

    // SMULWB / SMULWT: (a * halfword) >> 16, halfword is signed
    EXPECT(signed_multiply_32x16b(0x10000, 0x8000), -32768); // b low half = -32768
    EXPECT(signed_multiply_32x16t(0x10000, 0x80000000u), -32768);
    EXPECT(signed_multiply_32x16b(INT32_MAX, 0x7FFF), 1073709055LL);

    // SMMUL / SMMULR: high word, rounded variant adds 0x80000000
    EXPECT(multiply_32x32_rshift32(0x40000000, 0x40000000), 0x10000000);
    EXPECT(multiply_32x32_rshift32(-1, 1), -1); // floor, not truncate
    EXPECT(multiply_32x32_rshift32_rounded(-1, 1), 0);
    EXPECT(multiply_32x32_rshift32_rounded(0x00010000, 0x00008000), 1); // (2^31 + 2^31) >> 32
    EXPECT(multiply_32x32_rshift32_rounded(INT32_MIN, INT32_MIN), 0x40000000);

    // SMMLAR / SMMLSR keep the 32-bit result wrapping
    EXPECT(multiply_accumulate_32x32_rshift32_rounded(1, 0x40000000, 0x40000000), 0x10000001);
    EXPECT(multiply_accumulate_32x32_rshift32_rounded(INT32_MAX, 0x40000000, 0x40000000), (int32_t)(INT32_MAX + 0x10000000u));
    EXPECT(multiply_subtract_32x32_rshift32_rounded(0, 0x40000000, 0x40000000), -0x10000000);

    // PKH
    EXPECT(pack_16t_16t(0x12340000, 0x56780000), 0x12345678);
    EXPECT(pack_16t_16b(0x12340000, 0x0000ABCD), 0x1234ABCD);
    EXPECT(pack_16b_16b(0x00001234, 0x0000ABCD), 0x1234ABCD);

    // QADD16 / QSUB16 saturate each halfword independently
    EXPECT(signed_add_16_and_16(0x7FFF8000u, 0x0001FFFFu), 0x7FFF8000u);
    EXPECT(signed_add_16_and_16(0x00010002u, 0x00030004u), 0x00040006u);
    EXPECT((uint32_t)signed_subtract_16_and_16((int32_t)0x80007FFFu, 0x00010001), 0x80007FFEu);
    EXPECT((uint32_t)signed_subtract_16_and_16(0x00000000, 0x7FFF8000), 0x80017FFFu); // lo: 0-(-32768) saturates

    // SHADD16 / SHSUB16 halve without saturating
    EXPECT((uint32_t)signed_halving_add_16_and_16((int32_t)0x7FFF7FFF, (int32_t)0x7FFF7FFF), 0x7FFF7FFFu);
    EXPECT((uint32_t)signed_halving_add_16_and_16((int32_t)0xFFFFFFFF, (int32_t)0xFFFFFFFE), 0xFFFFFFFEu); // hi (-2)>>1=-1, lo (-3)>>1=-2
    EXPECT((uint32_t)signed_halving_subtract_16_and_16(0x00020004, 0x00000000), 0x00010002u);

    // SMLAWB / SMLAWT
    EXPECT(signed_multiply_accumulate_32x16b(5, 0x10000, 0x00000002), 5 + 2);
    EXPECT(signed_multiply_accumulate_32x16t(5, 0x10000, 0x00020000), 5 + 2);

    // SMUAD(X) and the 64-bit SMLALD(X)
    EXPECT(multiply_16tx16t_add_16bx16b(0x00020003, 0x00040005), 2 * 4 + 3 * 5);
    EXPECT(multiply_16tx16t_add_16bx16b(0x80008000u, 0x80008000u), (int32_t)(2u * 0x40000000u)); // wraps like SMUAD
    EXPECT(multiply_16tx16b_add_16bx16t(0x00020003, 0x00040005), 2 * 5 + 3 * 4);
    EXPECT(multiply_accumulate_16tx16t_add_16bx16b(1000000000000LL, 0x00020003, 0x00040005), 1000000000000LL + 23);
    EXPECT(multiply_accumulate_16tx16b_add_16bx16t(-5, 0x00020003, 0x00040005), -5 + 22);

    // SMULxy
    EXPECT(multiply_16bx16b(0xFFFF0003u, 0x00010002u), 6);
    EXPECT(multiply_16bx16t(0x00000003u, 0xFFFF0000u), -3);
    EXPECT(multiply_16tx16b(0x00020000u, 0x00000005u), 10);
    EXPECT(multiply_16tx16t(0x80000000u, 0x80000000u), 0x40000000);

    // QSUB saturates to the 32-bit range
    EXPECT(substract_32_saturate((uint32_t)INT32_MIN, 1), INT32_MIN);
    EXPECT(substract_32_saturate((uint32_t)INT32_MAX, (uint32_t)-1), INT32_MAX);
    EXPECT(substract_32_saturate(10, 3), 7);

    EXPECT(multiply_32x32_rshift(0x40000000, 0x40000000, 0), 0x20000000);

    if (failures == 0) printf("dspinst_test OK\n");
    return failures ? 1 : 0;
}
