/*
 * The game clock on WarpOS.
 *
 * timer.device's ReadEClock is a 68k call from a PPC task: about 0.4 ms per
 * read on the reference A4000 (MPC7410), and the game reads its clock many
 * times a frame. The PPC time base costs a few instructions. Its rate (the
 * bus clock / 4) differs between boards, so it is measured against DateStamp
 * (50 Hz ticks): over five ticks at the first call, then refined over the
 * whole run about once a second. Time is accumulated step by step, so a
 * refined rate never makes the clock jump or run backwards.
 */
#include "platform/warpos/wos_clock.h"

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include "platform/amiga/amiga_os_end.h"

static uint64_t ReadTimeBase(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb  %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static int64_t DateStampTicks(void)
{
    struct DateStamp stamp;
    DateStamp(&stamp);
    return (int64_t)stamp.ds_Days * 4320000 + (int64_t)stamp.ds_Minute * 3000 + stamp.ds_Tick;
}

static int s_started;
static uint64_t s_rate;                 /* time base ticks per second */
static uint64_t s_last, s_calStart, s_calLast;
static int64_t s_calStartTicks;
static uint64_t s_micros, s_remainder;

static int PlausibleRate(uint64_t rate)
{
    return rate >= 1000000u && rate <= 400000000u;
}

static void Start(void)
{
    int64_t tick = DateStampTicks(), edge;
    uint64_t t0, t1, rate;

    /* From one tick edge to the fifth after it: 100 ms. */
    while ((edge = DateStampTicks()) == tick)
        ;
    t0 = ReadTimeBase();
    while (DateStampTicks() < edge + 5)
        ;
    t1 = ReadTimeBase();
    rate = (t1 - t0) * 10u;
    s_rate = PlausibleRate(rate) ? rate : 25000000u;
    s_last = s_calStart = s_calLast = t1;
    s_calStartTicks = edge + 5;
    s_started = 1;
}

uint64_t WosClock_Micros(void)
{
    uint64_t now, step;

    if (!s_started) {
        Start();
        return 0;
    }
    now = ReadTimeBase();
    if (now - s_calLast >= s_rate) {
        const int64_t ticks = DateStampTicks() - s_calStartTicks;
        s_calLast = now;
        if (ticks >= 250) {
            const uint64_t rate = (now - s_calStart) * 50u / (uint64_t)ticks;
            if (PlausibleRate(rate))
                s_rate = rate;
        }
    }
    step = (now - s_last) * 1000000u + s_remainder;
    s_last = now;
    s_micros += step / s_rate;
    s_remainder = step % s_rate;
    return s_micros;
}

uint32_t WosClock_Rate(void)
{
    return (uint32_t)s_rate;
}
