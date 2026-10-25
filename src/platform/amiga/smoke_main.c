/*
 * M0.9 startup smoke harness. Domain B: MiniGL/NDK headers are expected here.
 *
 * Proves the cross toolchain, libnix startup, MiniGL 29.1, timer.device and
 * AHI before game code exists. It is not the game and does not use the port
 * layer. Run:
 *
 *     amiga_smoke RAM:m0-smoke.ppm > RAM:m0-smoke.log
 *
 * Copy the log and the PPM to build/amiga/ afterward. The PiStorm run remains
 * M0.9's target evidence; an emulator run is not a substitute.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#define SMOKE_WIDTH  640
#define SMOKE_HEIGHT 480
#define SMOKE_SECONDS 2
#define RAWKEY_ESC 0x45

static struct AHIRequest g_ahi;
static struct timerequest g_timer;
static int g_ahi_open;
static int g_timer_open;
static int g_minigl_open;
static int g_exit_requested;
static void *g_context;
static ULONG g_start_secs;
static ULONG g_start_micros;
static ULONG g_last_tick;

static void LogStack(const char *phase)
{
    struct Task *task = FindTask(NULL);
    char marker;
    ULONG lower = (ULONG)task->tc_SPLower;
    ULONG upper = (ULONG)task->tc_SPUpper;
    ULONG here = (ULONG)&marker;

    if (here >= lower && here <= upper) {
        printf("smoke: stack %s used=%lu free=%lu total=%lu\n",
               phase, (unsigned long)(upper - here), (unsigned long)(here - lower),
               (unsigned long)(upper - lower));
    }
}

static int ReadTime(ULONG *secs, ULONG *micros)
{
    if (!g_timer_open)
        return 0;
    g_timer.tr_node.io_Command = TR_GETSYSTIME;
    if (DoIO((struct IORequest *)&g_timer) != 0)
        return 0;
    *secs = g_timer.tr_time.tv_secs;
    *micros = g_timer.tr_time.tv_micro;
    return 1;
}

static ULONG ElapsedSeconds(ULONG secs, ULONG micros)
{
    if (secs < g_start_secs)
        return 0;
    if (secs == g_start_secs && micros < g_start_micros)
        return 0;
    return secs - g_start_secs;
}

static void OnKey(char key)
{
    printf("smoke: key %d\n", (int)(unsigned char)key);
    if ((unsigned char)key == RAWKEY_ESC || (unsigned char)key == 27) {
        printf("smoke: escape\n");
        g_exit_requested = 1;
        mglExit();
    }
}

static void OnIdle(void)
{
    ULONG secs = 0;
    ULONG micros = 0;

    if (g_exit_requested)
        return;
    if (!ReadTime(&secs, &micros)) {
        printf("smoke: timer read failed\n");
        g_exit_requested = 1;
        mglExit();
        return;
    }
    if (secs != g_last_tick) {
        g_last_tick = secs;
        printf("smoke: tick %lu\n", (unsigned long)secs);
    }
    if (ElapsedSeconds(secs, micros) >= SMOKE_SECONDS) {
        printf("smoke: timer done\n");
        g_exit_requested = 1;
        mglExit();
    }
}

static int OpenAhi(void)
{
    memset(&g_ahi, 0, sizeof(g_ahi));
    g_ahi.ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)"ahi.device", AHI_DEFAULT_UNIT,
                   (struct IORequest *)&g_ahi, 0) != 0) {
        printf("smoke: AHI open failed\n");
        return 0;
    }
    g_ahi_open = 1;
    printf("smoke: AHI opened\n");
    return 1;
}

static int OpenTimer(void)
{
    memset(&g_timer, 0, sizeof(g_timer));
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
                   (struct IORequest *)&g_timer, 0) != 0) {
        printf("smoke: timer.device open failed\n");
        return 0;
    }
    g_timer_open = 1;
    printf("smoke: timer.device opened\n");
    return 1;
}

static void CloseAll(void)
{
    if (g_context != NULL) {
        mglDeleteContext();
        g_context = NULL;
    }
    if (g_minigl_open) {
        MiniGLClose();
        g_minigl_open = 0;
    }
    if (g_timer_open) {
        CloseDevice((struct IORequest *)&g_timer);
        g_timer_open = 0;
        printf("smoke: timer.device closed\n");
    }
    if (g_ahi_open) {
        CloseDevice((struct IORequest *)&g_ahi);
        g_ahi_open = 0;
        printf("smoke: AHI closed\n");
    }
}

int main(int argc, char **argv)
{
    const char *shot = argc > 1 ? argv[1] : (const char *)"m0-smoke.ppm";
    ULONG secs = 0;
    ULONG micros = 0;

    printf("smoke: start\n");
    LogStack("start");
    if (!OpenAhi())
        goto fail;
    if (!OpenTimer())
        goto fail;
    if (!ReadTime(&g_start_secs, &g_start_micros)) {
        printf("smoke: initial timer read failed\n");
        goto fail;
    }
    g_last_tick = g_start_secs;

    if (!MiniGLOpen()) {
        printf("smoke: MiniGLOpen failed\n");
        goto fail;
    }
    g_minigl_open = 1;

    g_context = mglCreateContext(0, 0, SMOKE_WIDTH, SMOKE_HEIGHT);
    if (g_context == NULL) {
        printf("smoke: MGLCreateContext failed\n");
        goto fail;
    }

    mglKeyFunc(OnKey);
    mglIdleFunc(OnIdle);

    if (!mglLockDisplay()) {
        printf("smoke: mglLockDisplay failed\n");
        goto fail;
    }
    glClearColor(0.15f, 0.35f, 0.65f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglUnlockDisplay();
    mglSwitchDisplay();
    if (mglLockDisplay()) {
        mglWriteShotPPM((char *)shot);
        mglUnlockDisplay();
        printf("smoke: screenshot %s\n", shot);
    } else {
        printf("smoke: screenshot lock failed\n");
    }

    fflush(stdout);
    printf("smoke: entering main loop\n");
    mglMainLoop();
    printf("smoke: main loop ended\n");
    if (!ReadTime(&secs, &micros))
        printf("smoke: final timer read failed\n");
    else
        printf("smoke: elapsed %lu s\n", (unsigned long)ElapsedSeconds(secs, micros));
    LogStack("end");

    CloseAll();
    printf("smoke: OK\n");
    fflush(stdout);
    return 0;

fail:
    CloseAll();
    printf("smoke: FAIL\n");
    fflush(stdout);
    return 10;
}
