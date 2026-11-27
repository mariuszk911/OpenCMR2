/*
 * wos_sys.h -- the WarpOS build's clock, caches, log, ENV: and library setup.
 * Plain C types only, so any file of the program can include it.
 */
#ifndef WOS_SYS_H
#define WOS_SYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int      wos_sys_init(void);          /* open the libraries; 0 on success */
void     wos_sys_shutdown(void);

uint64_t wos_now_us(void);            /* the PPC's time base; never leaves the PPC */
void     wos_wait_until_us(uint64_t t);  /* spins: an OS wait oversleeps by ~13 ms */
uint32_t wos_tb_hz(void);

static inline uint32_t wos_tb_lo(void)
{
    uint32_t lo;
    __asm__ volatile("mftb %0" : "=r"(lo));
    return lo;
}
/* Time a statement into a bucket of time-base ticks: WOS_TIME(ticks[i], f()); */
#define WOS_TIME(acc, stmt) do { uint32_t _t0 = wos_tb_lo(); stmt; (acc) += wos_tb_lo() - _t0; } while (0)

/* Make freshly written code visible to the instruction fetch (a JIT). */
void     wos_cache_flush_code(void *start, uint32_t len);
/* Push data out of the PPC's cache before the 68k reads it / drop the PPC's
 * stale copy of what the 68k wrote. NEVER on an exec MsgPort or other
 * 68k-owned list. */
void     wos_cache_flush_data(void *start, uint32_t len);
void     wos_cache_invalidate_data(void *start, uint32_t len);

/* Memory the PPC executes and reads fastest: 32-byte aligned. */
void    *wos_alloc_ppc(uint32_t size);
void     wos_free_ppc(void *p);

/* The log: collected in memory, written every 2 s and at exit. */
void     wos_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void     wos_log_flush(void);
void     wos_log_tick(void);          /* once a frame: writes when 2 s have passed */

/* Breadcrumbs for hangs: [0] the last site passed, [1..7] the program's own.
 * Pushed to memory at every write, so the 68k side (a debugger, a bridge)
 * can read them while the program hangs; the address is in the log. */
extern volatile uint32_t wos_crumb[8];
#ifdef WOS_CRUMBS               /* make WOS_CRUMBS=1: a dcbst+sync each, slow */
#define WOS_CRUMB(n) do { wos_crumb[0] = (n); __asm__ volatile("dcbst 0,%0\n\tsync" : : "r"(wos_crumb) : "memory"); } while (0)
#define WOS_CRUMB_SET(i, v) (wos_crumb[i] = (uint32_t)(v))
#else
#define WOS_CRUMB(n) do { } while (0)
#define WOS_CRUMB_SET(i, v) do { } while (0)
#endif

/* The first PPC exception's context, for reading from the 68k afterwards:
 * [0] 0xe5c0 once set, [1] id, [2] srr0 (pc), [3] srr1, [4] dar, [5] dsisr,
 * [6] lr, [7] ctr, [8..39] r0..r31. */
extern volatile uint32_t wos_exc[40];

/* Run from the program's own drawer: Lock("PROGDIR:") + CurrentDir, never
 * newlib chdir (it unlocks the Shell's own lock). */
void     wos_enter_progdir(void);
void     wos_leave_progdir(void);
/* A path relative to the current directory made absolute, before
 * wos_enter_progdir changes what it is relative to. */
void     wos_make_absolute(char *path, unsigned size);

/* ENV: through dos.library GetVar: newlib's getenv() finds nothing. */
const char *wos_getenv(const char *name);   /* static buffer, or NULL */
int      wos_env_int(const char *name, int def);
int      wos_env_take(const char *name);    /* 1 when set, and deletes it */

#ifdef __cplusplus
}
#endif

#endif
