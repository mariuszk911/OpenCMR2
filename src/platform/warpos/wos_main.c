/*
 * wos_main.c -- the smallest WarpOS program on this scaffolding: build it
 * first, run it on the target, and only then start on the real program.
 * It proves the toolchain, the ET_REL link, the converter, the stack, the
 * log and the clock before any of the program's own code can be blamed.
 */
#include <stdio.h>

#include "wos_sys.h"

int main(int argc, char **argv)
{
    uint64_t t0;
    int i;

    if (wos_sys_init() != 0) {
        wos_sys_shutdown();
        return 20;
    }
    for (i = 0; i < argc; i++)
        wos_log("arg %d: %s", i, argv[i]);

    t0 = wos_now_us();
    wos_wait_until_us(t0 + 2500000u);       /* past one clock calibration */
    wos_log("2.5 s by the time base: %lu us, %lu Hz",
            (unsigned long)(wos_now_us() - t0), (unsigned long)wos_tb_hz());
    printf("hello from the PPC; see the log\n");

    wos_sys_shutdown();
    return 0;
}
