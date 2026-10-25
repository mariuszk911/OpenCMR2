// System services on AmigaOS: see port/sys.h.
//
// This first slice covers the clock, sleep and logging. File/path/settings,
// window/IDCMP and message boxes land with their M1 components, so the game
// cannot boot on this file alone yet.
#include "port/sys.h"
#include "port/sys_steps.h"
#include "platform/amiga/amiga_sys.h"
#include "platform/amiga/amiga_sys_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

// proto/timer.h declares these; libnix supplies SysBase and DOSBase.
struct Device *TimerBase;

namespace {

struct MsgPort *s_timerPort;
struct timerequest s_timerRequest;
uint32_t s_eclockFrequency;
uint64_t s_eclockStart;
int s_timeState; // 0 = not tried, 1 = ready, -1 = failed to open

FILE *s_logFile;
int s_logTried;
char s_commandLine[1024];

int EClockNow(uint64_t *value)
{
    struct EClockVal clock;
    if (ReadEClock(&clock) == 0)
        return 0;
    *value = ((uint64_t)clock.ev_hi << 32) | clock.ev_lo;
    return 1;
}

int InitTime(void)
{
    if (s_timeState != 0)
        return s_timeState > 0;
    s_timeState = -1;

    s_timerPort = CreateMsgPort();
    if (s_timerPort == NULL) {
        Sys_Log("timer.device: CreateMsgPort failed");
        return 0;
    }
    memset(&s_timerRequest, 0, sizeof(s_timerRequest));
    s_timerRequest.tr_node.io_Message.mn_ReplyPort = s_timerPort;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&s_timerRequest, 0) != 0) {
        Sys_Log("timer.device: OpenDevice failed (io_Error %ld)",
                (long)s_timerRequest.tr_node.io_Error);
        DeleteMsgPort(s_timerPort);
        s_timerPort = NULL;
        return 0;
    }

    TimerBase = s_timerRequest.tr_node.io_Device;
    struct EClockVal clock;
    s_eclockFrequency = ReadEClock(&clock);
    if (s_eclockFrequency == 0) {
        Sys_Log("timer.device: ReadEClock is not available");
        CloseDevice((struct IORequest *)&s_timerRequest);
        DeleteMsgPort(s_timerPort);
        TimerBase = NULL;
        s_timerPort = NULL;
        return 0;
    }
    s_eclockStart = ((uint64_t)clock.ev_hi << 32) | clock.ev_lo;
    s_timeState = 1;
    return 1;
}

void CloseTime(void)
{
    if (s_timerPort == NULL)
        return;
    CloseDevice((struct IORequest *)&s_timerRequest);
    DeleteMsgPort(s_timerPort);
    s_timerPort = NULL;
    TimerBase = NULL;
    s_timeState = -1;
}

} // namespace

extern "C" void AmigaSys_SetCommandLine(const char *line)
{
    if (line == NULL)
        line = "";
    strncpy(s_commandLine, line, sizeof(s_commandLine) - 1);
    s_commandLine[sizeof(s_commandLine) - 1] = '\0';
}

extern "C" uint32_t Sys_GetTicks(void)
{
    uint64_t now;
    if (!InitTime() || !EClockNow(&now))
        return 0;
    // The E-clock is monotonic; the subtraction wraps safely in 64 bits.
    return AmigaSys_Milliseconds(now - s_eclockStart, s_eclockFrequency);
}

extern "C" double Sys_GetTicksPrecise(void)
{
    uint64_t now;
    if (!InitTime() || !EClockNow(&now))
        return 0.0;
    return AmigaSys_MillisecondsPrecise(now - s_eclockStart, s_eclockFrequency);
}

extern "C" unsigned int Sys_GetUnixTime(void)
{
    struct DateStamp stamp;
    DateStamp(&stamp);
    return AmigaSys_UnixSeconds(stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick);
}

extern "C" int Sys_TakeSteps(SysStepClock *clock, int rate, int max)
{
    return Sys_TakeStepsAt(clock, Sys_GetTicks(), rate, max);
}

extern "C" void Sys_Sleep(uint32_t ms)
{
    if (ms == 0 || !InitTime())
        return;
    s_timerRequest.tr_node.io_Command = TR_ADDREQUEST;
    s_timerRequest.tr_time.tv_secs = ms / 1000u;
    s_timerRequest.tr_time.tv_micro = (ms % 1000u) * 1000u;
    if (DoIO((struct IORequest *)&s_timerRequest) != 0)
        Sys_Log("timer.device: TR_ADDREQUEST failed (io_Error %ld)",
                (long)s_timerRequest.tr_node.io_Error);
}

extern "C" void Sys_Log(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "%s\n", line);
    if (!s_logTried) {
        s_logTried = 1;
        s_logFile = fopen("PROGDIR:opencmr2.log", "a");
        if (s_logFile == NULL)
            fprintf(stderr, "opencmr2: cannot open PROGDIR:opencmr2.log\n");
    }
    if (s_logFile != NULL) {
        fprintf(s_logFile, "%s\n", line);
        fflush(s_logFile);
    }
}

extern "C" void Sys_Fatal(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Sys_Log("fatal: %s", line);
    // The requester lands with the Intuition/IDCMP slice of M1.
    Sys_Exit(1);
}

extern "C" void Sys_Exit(int code)
{
    CloseTime();
    if (s_logFile != NULL) {
        fclose(s_logFile);
        s_logFile = NULL;
    }
    exit(code);
}

extern "C" const char *Sys_GetCommandLine(void)
{
    return s_commandLine;
}
