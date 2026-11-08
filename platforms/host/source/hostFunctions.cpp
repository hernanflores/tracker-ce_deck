// Free functions that the firmware core expects the platform to provide.
#include "HostPlatform.h"

#include "log.h"
#include "platform_math.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

std::atomic<bool> gAudioOutMuted{ false };

void muteAudioOutByKey()
{
    gAudioOutMuted = true;
}

void unmuteAudioOutByKey()
{
    gAudioOutMuted = false;
}

uint8_t isAudioOutMuted()
{
    return gAudioOutMuted ? 1 : 0;
}

// Q15 sine; the input maps [0, 32767] onto one full period like CMSIS arm_sin_q15.
int16_t sin_q15(int16_t value)
{
    const double phase = (value & 0x7FFF) / 32768.0;
    long v = lround(sin(2.0 * M_PI * phase) * 32768.0);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

void _debuglog(const char* msg)
{
    fprintf(stderr, "%s\n", msg);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void assert_func_play(const char* file, int line)
{
    fprintf(stderr, "ASSERT failed: %s:%d\n", file, line);
    abort();
}
