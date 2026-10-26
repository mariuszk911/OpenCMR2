/* libnix swapstack.o swaps to this many bytes at startup. The Shell/Workbench
   stack is far too small for the C++ game and the STL-heavy Sys layer. */
#ifndef OPENCMR2_AMIGA_STACK
#define OPENCMR2_AMIGA_STACK 2097152UL
#endif

unsigned long __stack = OPENCMR2_AMIGA_STACK;
