// Portable Sys time logic: the fixed-step clock shared with the host, and the
// Amiga E-clock/DateStamp conversions. The timer.device calls themselves are
// covered by the Amiga build and the target smoke.
#include "platform/amiga/amiga_sys_time.h"
#include "port/sys.h"
#include "port/sys_steps.h"

#include <cmath>
#include <cstdio>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    SysStepClock clock = {0, 0, 0};
    Check(Sys_TakeStepsAt(&clock, 1000, 50, 5) == 1, "first call returns one step");
    Check(clock.started == 1 && clock.last == 1000 && clock.remainder == 0, "first call state");

    Check(Sys_TakeStepsAt(&clock, 1020, 50, 5) == 1, "twenty milliseconds is one step");
    Check(clock.remainder == 0, "exact step leaves no remainder");

    Check(Sys_TakeStepsAt(&clock, 1030, 50, 5) == 0, "ten milliseconds is no step");
    Check(clock.remainder == 500, "half a step is carried");

    Check(Sys_TakeStepsAt(&clock, 1040, 50, 5) == 1, "carried remainder completes a step");
    Check(clock.remainder == 0, "carried remainder is cleared");

    Check(Sys_TakeStepsAt(&clock, 1200, 50, 3) == 3, "steps are clamped to max");
    Check(clock.remainder == 0, "clamped steps still consume the whole units");

    Check(Sys_TakeStepsAt(&clock, 1500, 50, 5) == 1, "a pause resets to one step");
    Check(clock.remainder == 0 && clock.started == 1, "pause resets the remainder");

    SysStepClock wrapped = {0xfffffff0u, 0, 1};
    Check(Sys_TakeStepsAt(&wrapped, 0x20, 50, 5) == 2, "the millisecond clock wraps normally");
    Check(wrapped.remainder == 400, "wrapped steps keep the remainder");

    SysStepClock pauseWrap = {0xffffff00u, 0, 1};
    Check(Sys_TakeStepsAt(&pauseWrap, 0x10, 50, 5) == 1,
          "a pause across the wrap resets to one step");
    Check(pauseWrap.remainder == 0, "pause across the wrap resets the remainder");

    SysStepClock zeroRate = {0, 0, 0};
    Check(Sys_TakeStepsAt(&zeroRate, 100, 0, 5) == 1, "zero rate still returns the first step");
    Check(Sys_TakeStepsAt(&zeroRate, 110, 0, 5) == 0, "zero rate never advances");

    Check(AmigaSys_Milliseconds(709379, 709379) == 1000, "E-clock milliseconds");
    Check(AmigaSys_Milliseconds(1, 709379) == 0, "sub-millisecond E-clock is truncated");
    Check(AmigaSys_Milliseconds(123, 0) == 0, "zero E-clock frequency is rejected");
    Check(AmigaSys_Milliseconds(0x100000000ull, 709379) == 6054545,
          "E-clock elapsed above 2^32 ticks keeps 64-bit precision");
    Check(AmigaSys_Milliseconds(3046760480842ull, 709379) == 1233u,
          "E-clock milliseconds truncate to 32 bits");
    Check(std::fabs(AmigaSys_MillisecondsPrecise(354689, 709379) - 500.0) < 0.001,
          "precise E-clock milliseconds");

    Check(AmigaSys_UnixSeconds(0, 0, 0) == 252460800u, "AmigaDOS epoch in Unix seconds");
    Check(AmigaSys_UnixSeconds(8035, 0, 0) == 946684800u, "2000-01-01 in Unix seconds");
    Check(AmigaSys_UnixSeconds(0, 1, 0) == 252460860u, "one minute is sixty seconds");
    Check(AmigaSys_UnixSeconds(0, 0, 49) == 252460800u, "ticks below one second");
    Check(AmigaSys_UnixSeconds(0, 0, 50) == 252460801u, "fifty ticks is one second");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sys time: ok\n");
    return 0;
}
