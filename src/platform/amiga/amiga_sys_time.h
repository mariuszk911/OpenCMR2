/*
 * Pure time conversions for amiga_sys.cpp, kept free of NDK headers so the
 * host test can check them.
 */
#ifndef OPENCMR2_AMIGA_SYS_TIME_H
#define OPENCMR2_AMIGA_SYS_TIME_H

#include <stdint.h>

static inline uint32_t AmigaSys_Milliseconds(uint64_t elapsed, uint32_t frequency)
{
    if (frequency == 0)
        return 0;
    return (uint32_t)(elapsed * 1000u / frequency);
}

static inline double AmigaSys_MillisecondsPrecise(uint64_t elapsed, uint32_t frequency)
{
    if (frequency == 0)
        return 0.0;
    return (double)elapsed * 1000.0 / (double)frequency;
}

/* days/minutes/ticks are the AmigaDOS DateStamp fields; ticks run at 50 Hz.
   252460800 is 1978-01-01 in Unix seconds. DateStamp is local time, so this is
   the local clock read as if it were UTC; the game uses it only as a changing
   random seed. */
static inline unsigned int AmigaSys_UnixSeconds(int32_t days, int32_t minutes, int32_t ticks)
{
    return (unsigned int)((uint32_t)days * 86400u + (uint32_t)minutes * 60u +
                          (uint32_t)ticks / 50u + 252460800u);
}

#endif
