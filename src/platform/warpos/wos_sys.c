/*
 * wos_sys.c -- the WarpOS build's clock, caches, log and library bases.
 *
 * Every OS call from PPC code is a trip to the 68k and back (1-5 ms on an
 * A4000 with an MPC7410), so what a frame needs often -- the clock, the
 * waits, the cache maintenance, the log -- stays on the PPC: the time base,
 * spinning waits, the PPC's own dcbst/dcbf/icbi, and a log written by a DOS
 * packet nobody waits for.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "wos_sys.h"
#include "wos_amiga.h"

#ifndef WOS_BUILD_STAMP
#define WOS_BUILD_STAMP "unknown"
#endif
#ifndef WOS_NAME
#define WOS_NAME "Program"
#endif
#ifndef WOS_LOG_PATH
#define WOS_LOG_PATH "PROGDIR:program.log"
#endif
#ifndef WOS_ENV_PREFIX              /* WOS_ENV_PREFIX "CLOCK", "LOGASYNC" */
#define WOS_ENV_PREFIX "WOS_"
#endif

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
extern struct Library *PowerPCBase;

#define CACHE_LINE 32

/* ---- The clock -------------------------------------------------------- */

/* The PPC's time base, read without leaving the PPC. GetSysTimePPC has
 * crashed from a MOS2WOS PPC task on an A4000/MPC7410, so the time base is
 * the default; its rate (bus/4) is measured against DateStamp about once a
 * second. SetEnv <PREFIX>CLOCK sys selects GetSysTimePPC. */
static uint64_t read_timebase(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb  %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static int64_t datestamp_ticks(void)        /* 1/50 s */
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (int64_t)ds.ds_Days * 4320000 + (int64_t)ds.ds_Minute * 3000 + ds.ds_Tick;
}

static int      s_clock_sys = -1;
static uint64_t s_tb_hz = 24740000u;
static uint64_t s_tb_last, s_tb_cal0, s_tb_cal_last;
static int64_t  s_ds_cal0;
static uint64_t s_us, s_us_rem;

uint64_t wos_now_us(void)
{
    uint64_t tb, step;

    if (s_clock_sys < 0) {
        const char *v = wos_getenv(WOS_ENV_PREFIX "CLOCK");
        s_clock_sys = (v && !strcmp(v, "sys")) ? 1 : 0;
    }
    if (s_clock_sys) {
        struct timeval tv;
        GetSysTimePPC(&tv);
        return (uint64_t)tv.tv_secs * 1000000u + tv.tv_micro;
    }

    tb = read_timebase();
    if (!s_tb_last) {
        s_tb_last = s_tb_cal0 = s_tb_cal_last = tb;
        s_ds_cal0 = datestamp_ticks();
        return 0;
    }
    if (tb - s_tb_cal_last >= s_tb_hz) {
        int64_t ds = datestamp_ticks() - s_ds_cal0;
        s_tb_cal_last = tb;
        if (ds >= 50) {
            uint64_t hz = (tb - s_tb_cal0) * 50u / (uint64_t)ds;
            if (hz >= 5000000u && hz <= 100000000u)
                s_tb_hz = hz;
        }
    }
    step = (tb - s_tb_last) * 1000000u + s_us_rem;
    s_tb_last = tb;
    s_us += step / s_tb_hz;
    s_us_rem = step % s_tb_hz;
    return s_us;
}

uint32_t wos_tb_hz(void)
{
    return (uint32_t)s_tb_hz;
}

void wos_wait_until_us(uint64_t t)
{
    while (wos_now_us() < t)
        ;
}

/* ---- Caches ----------------------------------------------------------- */

void wos_cache_flush_code(void *start, uint32_t len)
{
    uintptr_t p   = (uintptr_t)start & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)start + len;
    uintptr_t q;

    for (q = p; q < end; q += CACHE_LINE)
        __asm__ volatile("dcbst 0,%0" : : "r"(q) : "memory");
    __asm__ volatile("sync" : : : "memory");
    for (q = p; q < end; q += CACHE_LINE)
        __asm__ volatile("icbi 0,%0" : : "r"(q) : "memory");
    __asm__ volatile("sync; isync" : : : "memory");
}

void wos_cache_flush_data(void *start, uint32_t len)
{
    uintptr_t p   = (uintptr_t)start & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)start + len;

    for (; p < end; p += CACHE_LINE)
        __asm__ volatile("dcbf 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

void wos_cache_invalidate_data(void *start, uint32_t len)
{
    /* dcbf, not dcbi: a partial line at either end may hold our own writes. */
    wos_cache_flush_data(start, len);
}

/* ---- Memory ----------------------------------------------------------- */

void *wos_alloc_ppc(uint32_t size)
{
    return AllocVecPPC(size, MEMF_PUBLIC | MEMF_CLEAR, CACHE_LINE);
}

void wos_free_ppc(void *p)
{
    if (p)
        FreeVecPPC(p);
}

/* ---- Environment ------------------------------------------------------ */

const char *wos_getenv(const char *name)
{
    static char buf[256];
    if (GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof(buf), 0) < 0)
        return NULL;
    return buf;
}

int wos_env_take(const char *name)
{
    char buf[4];
    if (GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof(buf), 0) < 0)
        return 0;
    DeleteVar((CONST_STRPTR)name, GVF_GLOBAL_ONLY);
    DeleteVar((CONST_STRPTR)name, GVF_LOCAL_ONLY);
    return 1;
}

int wos_env_int(const char *name, int def)
{
    const char *v = wos_getenv(name);
    return (v && *v) ? atoi(v) : def;
}

/* ---- The log ---------------------------------------------------------- */

/* A log line per event written straight to disk cost one port 60 ms a
 * frame: the lines are collected here and written every 2 s and at exit.
 * The flush is a DOS packet the PPC does not wait for: the file stays open,
 * the lines go out of the other buffer, and only the next flush waits for
 * the previous write, long done by then. (An open, append and close every
 * 2 s stopped the PPC 20-28 ms.) At the shutdown and in the exit hooks the
 * writes are synchronous. SetEnv <PREFIX>LOGASYNC 0: fopen every time. */
#define LOG_BUF_SIZE (64 * 1024)

static char     s_log_bufs[2][LOG_BUF_SIZE];
static char    *s_log_buf = s_log_bufs[0];
static int      s_log_cur;
static uint32_t s_log_len;
static uint64_t s_log_last_us;
static int      s_log_started;
static int      s_shutting_down;

static BPTR s_lfh;
static struct FileHandle *s_lfhp;
static struct MsgPort *s_lport;
static struct StandardPacket *s_lpkt;
static int s_lpend, s_log_async = -1;

static void log_wait(void)
{
    if (s_lpend) {
        WaitPort(s_lport);
        GetMsg(s_lport);
        s_lpend = 0;
    }
}

static void wos_log_close(void)
{
    log_wait();
    s_log_async = 0;
    if (s_lfh)
        Close(s_lfh);
    s_lfh = 0;
    s_lfhp = NULL;
    if (s_lport)
        DeleteMsgPort(s_lport);
    s_lport = NULL;
}

static int log_open_async(void)
{
    if (s_lfh)
        return 1;
    if (!s_log_started) {
        BPTR f = Open((CONST_STRPTR)WOS_LOG_PATH, MODE_NEWFILE);
        if (f)
            Close(f);
        s_log_started = 1;
    }
    s_lfh = Open((CONST_STRPTR)WOS_LOG_PATH, MODE_READWRITE);
    if (!s_lfh)
        return 0;
    Seek(s_lfh, 0, OFFSET_END);
    s_lfhp = (struct FileHandle *)BADDR(s_lfh);
    s_lport = CreateMsgPort();
    if (!s_lpkt)
        s_lpkt = wos_alloc_ppc(sizeof(*s_lpkt));
    if (!s_lfhp->fh_Type || !s_lport || !s_lpkt) {
        wos_log_close();
        return 0;
    }
    atexit(wos_log_close);
    return 1;
}

void wos_log_flush(void)
{
    FILE *f;

    if (!s_log_len)
        return;
    if (s_log_async < 0)
        s_log_async = wos_env_int(WOS_ENV_PREFIX "LOGASYNC", 1);
    if (s_log_async && !s_shutting_down && log_open_async()) {
        struct StandardPacket *p = s_lpkt;
        log_wait();
        /* the file system reads these from the 68k side */
        wos_cache_flush_data(s_log_buf, s_log_len);
        memset(p, 0, sizeof(*p));
        p->sp_Msg.mn_Node.ln_Name = (char *)&p->sp_Pkt;
        p->sp_Msg.mn_ReplyPort = s_lport;
        p->sp_Pkt.dp_Link = &p->sp_Msg;
        p->sp_Pkt.dp_Port = s_lport;
        p->sp_Pkt.dp_Type = ACTION_WRITE;
        p->sp_Pkt.dp_Arg1 = s_lfhp->fh_Arg1;
        p->sp_Pkt.dp_Arg2 = (LONG)s_log_buf;
        p->sp_Pkt.dp_Arg3 = (LONG)s_log_len;
        wos_cache_flush_data(p, sizeof(*p));
        PutMsg(s_lfhp->fh_Type, &p->sp_Msg);
        s_lpend = 1;
        s_log_cur ^= 1;
        s_log_buf = s_log_bufs[s_log_cur];
        s_log_len = 0;
        return;
    }
    if (s_lfh) {
        log_wait();
        Write(s_lfh, s_log_buf, (LONG)s_log_len);
    } else {
        f = fopen(WOS_LOG_PATH, s_log_started ? "a" : "w");
        if (f) {
            fwrite(s_log_buf, 1, s_log_len, f);
            fclose(f);
        }
    }
    s_log_started = 1;
    s_log_len = 0;
}

void wos_log(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (s_log_len > LOG_BUF_SIZE - 512)
        wos_log_flush();
    va_start(ap, fmt);
    n = vsnprintf(s_log_buf + s_log_len, LOG_BUF_SIZE - s_log_len - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((uint32_t)n > LOG_BUF_SIZE - s_log_len - 2)
        n = LOG_BUF_SIZE - s_log_len - 2;
    s_log_len += n;
    if (!s_log_len || s_log_buf[s_log_len - 1] != '\n')
        s_log_buf[s_log_len++] = '\n';
}

volatile uint32_t wos_crumb[8] __attribute__((aligned(32)));

void wos_log_tick(void)
{
    uint64_t now = wos_now_us();

    if (now - s_log_last_us >= 2000000u) {
        s_log_last_us = now;
        wos_log_flush();
    }
}

/* ---- The program's drawer -------------------------------------------- */

static BPTR s_progdir_lock;
static BPTR s_old_dir;
static int  s_in_progdir;

void wos_enter_progdir(void)
{
    s_progdir_lock = Lock((STRPTR)"PROGDIR:", ACCESS_READ);
    if (s_progdir_lock) {
        s_old_dir = CurrentDir(s_progdir_lock);
        s_in_progdir = 1;
    }
}

void wos_leave_progdir(void)
{
    if (s_in_progdir) {
        CurrentDir(s_old_dir);
        UnLock(s_progdir_lock);
        s_in_progdir = 0;
    }
}

void wos_make_absolute(char *path, unsigned size)
{
    char buf[512];
    BPTR lock;

    if (!path[0] || strchr(path, ':'))
        return;
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock)
        return;
    if (NameFromLock(lock, (STRPTR)buf, sizeof(buf))) {
        strncpy(path, buf, size - 1);
        path[size - 1] = 0;
    }
    UnLock(lock);
}

/* ---- PPC exceptions ---------------------------------------------------- */

/* A data, instruction or program exception ends the PPC task without a
 * word in the log (its requester opens on the Workbench screen, behind the
 * program's own). The handler keeps the first one's context in wos_exc[]
 * and pushes it to memory; then WarpOS goes on as before. */
volatile uint32_t wos_exc[40] __attribute__((aligned(32)));
static APTR s_exc_handler;

static ULONG wos_exc_handler(struct EXCContext *ec)
{
    int i;
    if (wos_exc[0] != 0xe5c0) {
        wos_exc[1] = ec->ec_ExcID;
        wos_exc[2] = ec->ec_UPC.ec_SRR0;
        wos_exc[3] = ec->ec_SRR1;
        wos_exc[4] = ec->ec_DAR;
        wos_exc[5] = ec->ec_DSISR;
        wos_exc[6] = ec->ec_LR;
        wos_exc[7] = ec->ec_CTR;
        for (i = 0; i < 32; i++)
            wos_exc[8 + i] = ec->ec_GPR[i];
        wos_exc[0] = 0xe5c0;
        for (i = 0; i < 40; i += 8)
            __asm__ volatile("dcbst 0,%0" : : "r"(&wos_exc[i]) : "memory");
        __asm__ volatile("sync");
    }
    return EXCRETURN_NORMAL;
}

/* exit() from anywhere but our own shutdown (an assert is enough) would
 * leave no trace: log the return addresses up the stack. wos_log's run-time
 * address gives the load offset for addr2line against the debug ELF. */
static void wos_atexit(void)
{
    uint32_t *fp = (uint32_t *)__builtin_frame_address(0);
    char line[300];
    int i, len;

    if (s_shutting_down)
        return;
    len = snprintf(line, sizeof line, "[diag] exit() outside the shutdown, crumb %lu, stack (wos_log at %p):",
                   (unsigned long)wos_crumb[0], (void *)wos_log);
    /* SysV back chain: [0] the caller's frame, [1] its saved lr */
    for (i = 0; i < 24 && fp && ((uint32_t)fp & 3) == 0 && len < (int)sizeof line - 12; i++) {
        uint32_t *next = (uint32_t *)fp[0];
        if (!next || next <= fp)
            break;
        len += snprintf(line + len, sizeof line - len, " %08lx", (unsigned long)next[1]);
        fp = next;
    }
    wos_log("%s", line);
    s_shutting_down = 1;            /* synchronous from here on */
    wos_log_flush();
}

static void wos_exc_install(void)
{
    struct TagItem tags[] = {
        { EXCATTR_CODE, (ULONG)wos_exc_handler },
        { EXCATTR_DATA, 0 },
        { EXCATTR_TASK, 0 },
        { EXCATTR_EXCID, EXCF_MCHECK | EXCF_DACCESS | EXCF_IACCESS | EXCF_PROGRAM },
        { EXCATTR_FLAGS, EXCF_LOCAL | EXCF_LARGECONTEXT },
        { EXCATTR_NAME, (ULONG)WOS_NAME },
        { EXCATTR_PRI, 0 },
        { TAG_END, 0 }
    };
    atexit(wos_atexit);
    s_exc_handler = SetExcHandler(tags);
    wos_log("[diag] exception record at %p%s, wos_log at %p", (void *)wos_exc,
            s_exc_handler ? "" : " (no handler)", (void *)wos_log);
}

/* ---- Libraries -------------------------------------------------------- */

int wos_sys_init(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase       = (struct GfxBase *)OpenLibrary("graphics.library", 39);

    wos_log(WOS_NAME " WarpOS build %s (%s %s)", WOS_BUILD_STAMP, __DATE__, __TIME__);
#ifdef WOS_CRUMBS
    wos_log("[diag] crumbs at %p", (void *)wos_crumb);
#endif
    wos_exc_install();
    if (!IntuitionBase || !GfxBase) {
        wos_log("intuition.library or graphics.library v39 missing");
        return -1;
    }
    s_log_last_us = wos_now_us();
    return 0;
}

void wos_sys_shutdown(void)
{
    s_shutting_down = 1;
    if (s_exc_handler) {
        RemExcHandler(s_exc_handler);
        s_exc_handler = NULL;
    }
    wos_log_flush();
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    GfxBase = NULL;
    IntuitionBase = NULL;
}
