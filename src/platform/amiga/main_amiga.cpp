/*
 * M0 scaffold for the AmigaOS executable.
 *
 * The M0 milestones prove the cross toolchain, libnix startup, linker and SDK
 * pin before game code is built. M1 replaces this with the game entry point
 * and the MiniGL/OS facade; M0.9 exercises MiniGL in its own smoke target.
 */
#include <stdio.h>

#ifndef OPENCMR2_AMIGA_STACK
#define OPENCMR2_AMIGA_STACK 0
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("OpenCMR2 AmigaOS scaffold; stack=%lu\n", (unsigned long)OPENCMR2_AMIGA_STACK);
    return 0;
}
