/*
 * M1 Sys time/logging smoke for AmigaOS. Not staged; run from a Shell:
 *
 *     DH20:amiga_sys_test > DH20:sys_time.log
 *
 * It exercises timer.device (ReadEClock), Sleep, the fixed-step clock,
 * DateStamp and the log file. An emulator run is emulator evidence, not
 * PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stdio.h>

static int Fail(const char *what)
{
    Sys_Log("sys_time: FAIL %s", what);
    printf("sys_time: FAIL %s\n", what);
    return 1;
}

int main(void)
{
    AmigaSys_SetCommandLine("sys_time_test");
    Sys_Log("sys_time: begin (command line %s)", Sys_GetCommandLine());

    const uint32_t start = Sys_GetTicks();
    const double preciseStart = Sys_GetTicksPrecise();
    Sys_Sleep(120);
    const uint32_t end = Sys_GetTicks();
    const double preciseEnd = Sys_GetTicksPrecise();
    const uint32_t elapsed = end - start;

    if (elapsed < 80 || elapsed > 500)
        return Fail("Sys_Sleep/Sys_GetTicks");
    if (preciseEnd <= preciseStart)
        return Fail("Sys_GetTicksPrecise did not advance");

    const unsigned int unixTime = Sys_GetUnixTime();
    if (unixTime == 0)
        return Fail("Sys_GetUnixTime");

    SysStepClock clock = {0, 0, 0};
    if (Sys_TakeSteps(&clock, 50, 5) != 1)
        return Fail("first Sys_TakeSteps");
    Sys_Sleep(45);
    const int steps = Sys_TakeSteps(&clock, 50, 5);
    if (steps < 1 || steps > 3)
        return Fail("Sys_TakeSteps after 45 ms");

    Sys_Log("sys_time: ticks %lu..%lu (%lu ms), precise ok, unix %lu, steps %d",
            (unsigned long)start, (unsigned long)end, (unsigned long)elapsed,
            (unsigned long)unixTime, steps);
    printf("sys_time: OK\n");
    Sys_Exit(0);
    return 1;
}
