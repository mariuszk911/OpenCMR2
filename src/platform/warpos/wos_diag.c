/*
 * PPC exception record for WarpOS.
 *
 * A data, instruction or program exception ends the PPC task without a word
 * in the log: WarpOS opens its requester on the Workbench screen, behind the
 * game's own. The handler keeps the first exception's context in
 * g_wosExceptionRecord and pushes it out of the PPC's data cache, so a 68k-side
 * memory reader (a debugger, the remote bridge) can read it afterwards:
 *
 *   [0] 0xe5c0 once set   [1] exception id   [2] srr0 (pc)   [3] srr1
 *   [4] dar               [5] dsisr          [6] lr          [7] ctr
 *   [8..39] r0..r31
 *
 * The start-up log line gives the record's address and Sys_Log's run-time
 * address; Sys_Log's address in build/warpos/opencmr2.debug.elf (nm) minus the
 * run-time one is the offset to subtract from pc and lr for addr2line.
 */
#include "platform/warpos/wos_diag.h"
#include "port/sys.h"

#include <stdint.h>

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>
#include <utility/tagitem.h>
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>
#include "platform/amiga/amiga_os_end.h"

volatile uint32_t g_wosExceptionRecord[40] __attribute__((aligned(32)));
static APTR s_handler;

static ULONG ExceptionHandler(struct EXCContext *context)
{
    int i;
    if (g_wosExceptionRecord[0] != 0xe5c0) {
        g_wosExceptionRecord[1] = context->ec_ExcID;
        g_wosExceptionRecord[2] = context->ec_UPC.ec_SRR0;
        g_wosExceptionRecord[3] = context->ec_SRR1;
        g_wosExceptionRecord[4] = context->ec_DAR;
        g_wosExceptionRecord[5] = context->ec_DSISR;
        g_wosExceptionRecord[6] = context->ec_LR;
        g_wosExceptionRecord[7] = context->ec_CTR;
        for (i = 0; i < 32; i++)
            g_wosExceptionRecord[8 + i] = context->ec_GPR[i];
        g_wosExceptionRecord[0] = 0xe5c0;
        /* 40 words = 5 cache lines of 32 bytes; the record is 32-byte aligned. */
        for (i = 0; i < 40; i += 8)
            __asm__ volatile("dcbst 0,%0" : : "r"(&g_wosExceptionRecord[i]) : "memory");
        __asm__ volatile("sync");
    }
    return EXCRETURN_NORMAL;
}

void WosDiag_Install(void)
{
    struct TagItem tags[] = {
        { EXCATTR_CODE, (ULONG)ExceptionHandler },
        { EXCATTR_DATA, 0 },
        { EXCATTR_TASK, 0 },
        { EXCATTR_EXCID, EXCF_MCHECK | EXCF_DACCESS | EXCF_IACCESS | EXCF_PROGRAM },
        { EXCATTR_FLAGS, EXCF_LOCAL | EXCF_LARGECONTEXT },
        { EXCATTR_NAME, (ULONG)"OpenCMR2" },
        { EXCATTR_PRI, 0 },
        { TAG_END, 0 }
    };
    if (s_handler != NULL)
        return;
    s_handler = SetExcHandler(tags);
    Sys_Log("diag: PPC exception record at %p%s, Sys_Log at %p", (void *)g_wosExceptionRecord,
            s_handler != NULL ? "" : " (no handler installed)", (void *)Sys_Log);
}

void WosDiag_Remove(void)
{
    if (s_handler != NULL) {
        RemExcHandler(s_handler);
        s_handler = NULL;
    }
}
