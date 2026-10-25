/*
 * Fixed-step clock shared by the host and Amiga Sys implementations.
 *
 * Sys_TakeSteps returns the steps of `rate` per second since its last call,
 * one on the first call or after a pause of over a quarter second, at most
 * `max`. The caller passes its millisecond clock so both implementations use
 * the same logic and a test can drive it deterministically.
 */
#ifndef OPENCMR2_PORT_SYS_STEPS_H
#define OPENCMR2_PORT_SYS_STEPS_H

#include "port/sys.h"

static inline int Sys_TakeStepsAt(SysStepClock *clock, uint32_t now, int rate, int max)
{
    const uint32_t elapsed = now - clock->last;
    clock->last = now;
    if (!clock->started || elapsed > 250) {
        clock->started = 1;
        clock->remainder = 0;
        return 1;
    }
    const uint32_t units = clock->remainder + elapsed * (uint32_t)rate;
    clock->remainder = units % 1000u;
    const int steps = (int)(units / 1000u);
    return steps < max ? steps : max;
}

#endif
