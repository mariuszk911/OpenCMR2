/*
 * A sampling profiler for the WarpOS build: SetEnv OPENCMR2_PCPROF 1.
 *
 * The MPC7410's PMC1 counts processor cycles from 0x80000000 - period; when
 * it turns negative the performance monitor exception (MMCR0[PMXE], which the
 * 7410 clears when it fires) hands the interrupted pc and lr to the handler
 * below, which stores them and arms the next sample. When the buffer is full
 * WosPcProf_Poll writes PROGDIR:pcsamp.bin: "PCS1", the sample count, the
 * run-time address of Sys_Log (to relocate against the debug ELF), then
 * count x (pc, lr). Resolve with addr2line against
 * build/warpos/opencmr2.debug.elf after subtracting the load offset (Sys_Log's
 * run-time address minus its address in the ELF).
 *
 * Diagnostic only: nothing runs until the ENV: switch is seen, and only on a
 * 7400/7410 (checked through the PVR). Taken from the
 * WiiStation port's sampler (same board, same WarpOS).
 */
#include "platform/warpos/wos_pcprof.h"
#include "port/sys.h"

#include <stdint.h>

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>
#include <proto/dos.h>
#include "platform/amiga/amiga_os_end.h"

#define MTSPR(n, v) __asm__ volatile("mtspr " #n ",%0" : : "r"(v))
#define MFSPR(n) ({ uint32_t v_; __asm__ volatile("mfspr %0," #n : "=r"(v_)); v_; })

enum { kSamples = 65536, kPeriod = 40000 };   /* about 100 us between samples at 400 MHz */

static uint32_t s_samples[kSamples][2];
static volatile uint32_t s_count;
static volatile int s_state;   /* 0 off, 1 sampling, 2 full, 3 written */
static APTR s_handler;

static ULONG SampleHandler(struct EXCContext *context)
{
    const uint32_t n = s_count;
    if (s_state == 1) {
        if (n < kSamples) {
            s_samples[n][0] = context->ec_UPC.ec_SRR0;
            s_samples[n][1] = context->ec_LR;
            s_count = n + 1;
        }
        if (n + 1 >= kSamples)
            s_state = 2;
    }
    MTSPR(953, 0x80000000u - (uint32_t)kPeriod);
    if (s_state == 1)
        MTSPR(952, MFSPR(952) | 0x04008000u);   /* PMXE | PMC1CE */
    else
        MTSPR(952, MFSPR(952) & ~0x04008000u);
    return EXCRETURN_ABORT;
}

void WosPcProf_Start(void)
{
    struct TagItem tags[] = {
        { EXCATTR_CODE, (ULONG)SampleHandler },
        { EXCATTR_DATA, 0 },
        { EXCATTR_TASK, 0 },
        { EXCATTR_EXCID, EXCF_PERFMON },
        { EXCATTR_FLAGS, EXCF_GLOBAL | EXCF_LARGECONTEXT },
        { EXCATTR_NAME, (ULONG)"OpenCMR2 prof" },
        { EXCATTR_PRI, 0 },
        { TAG_END, 0 }
    };
    ULONG old;

    if (s_state != 0)
        return;
    {
        /* MMCR0/PMC1 as programmed below are the 7400/7410 layout; other
         * CPUs (a 603e has no performance monitor) are refused. */
        uint32_t pvr;
        ULONG previous = Super();   /* mfpvr is privileged */
        pvr = MFSPR(287);
        User(previous);
        if ((pvr >> 16) != 0x000cu && (pvr >> 16) != 0x800cu) {
            Sys_Log("pcprof: PVR %08lx is not a 7400/7410; sampler not started", (unsigned long)pvr);
            s_state = 3;
            return;
        }
    }
    s_handler = SetExcHandler(tags);
    if (s_handler == NULL) {
        Sys_Log("pcprof: SetExcHandler failed");
        s_state = 3;
        return;
    }
    Sys_Log("pcprof: sampling every %d cycles", (int)kPeriod);
    s_count = 0;
    s_state = 1;
    old = Super();
    MTSPR(952, 0x80000000u);                    /* freeze while setting up */
    MTSPR(953, 0x80000000u - (uint32_t)kPeriod);
    MTSPR(954, 0); MTSPR(957, 0); MTSPR(958, 0);
    MTSPR(956, 0);
    MTSPR(952, (1u << 6) | 0x04008000u);        /* PMC1 counts cycles; PMXE | PMC1CE */
    User(old);
}

void WosPcProf_Poll(void)
{
    uint32_t header[3];
    BPTR file;
    ULONG old;

    if (s_state != 2)
        return;
    old = Super();
    MTSPR(952, MFSPR(952) & ~0x04008000u);
    User(old);
    RemExcHandler(s_handler);
    s_handler = NULL;
    s_state = 3;
    header[0] = 0x50435331u;   /* "PCS1" */
    header[1] = s_count;
    header[2] = (uint32_t)(uintptr_t)Sys_Log;
    file = Open((STRPTR)"PROGDIR:pcsamp.bin", MODE_NEWFILE);
    if (file == 0) {
        Sys_Log("pcprof: cannot write PROGDIR:pcsamp.bin (IoErr %ld)", (long)IoErr());
        return;
    }
    Write(file, header, sizeof(header));
    Write(file, s_samples, (LONG)(s_count * 8u));
    Close(file);
    Sys_Log("pcprof: %lu samples written to PROGDIR:pcsamp.bin (Sys_Log at %p)", (unsigned long)s_count,
            (void *)Sys_Log);
}
